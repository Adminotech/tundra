// !ref: default_avatar.avatar
// !ref: crosshair.js

if (!server.IsRunning() && !framework.IsHeadless())
{
    engine.ImportExtension("qt.core");
    engine.ImportExtension("qt.gui");
    engine.IncludeFile("crosshair.js");
}

// A simple walking avatar with physics & 1st/3rd person camera
function SimpleAvatar(entity, comp)
{
    // Store the entity reference
    this.me = entity;

    this.rotateSpeed = 100.0;
    this.mouseRotateSensitivity = 0.2;
    this.moveForce = 15.0;
    this.flySpeedFactor = 0.25;
    this.dampingForce = 3.0;
    this.walkAnimSpeed = 0.5;
    this.avatarCameraHeight = 1.0;
    this.avatarMass = 10;

    // Tracking motion with entity actions
    this.motionX = 0;
    this.motionY = 0;
    this.motionZ = 0;

    // Client-side yaw, pitch & rotation state
    this.yaw = 0;
    this.pitch = 0;
    this.rotate = 0;

    // Needed bools for logic
    this.isServer = server.IsRunning();
    this.ownAvatar = false;
    this.flying = false;
    this.falling = false;
    this.crosshair = null;
    this.isMouseLookLockedOnX = true;

    // Animation detection
    this.standAnimName = "Stand";
    this.walkAnimName = "Walk";
    this.flyAnimName = "Fly";
    this.hoverAnimName = "Hover";
    this.animList = [this.standAnimName, this.walkAnimName, this.flyAnimName, this.hoverAnimName];

    this.animsDetected = false;
    this.listenGesture = false;

    // Create avatar on server, and camera & input context on client
    if (this.isServer)
        this.ServerInitialize();
    else
        this.ClientInitialize();
}

SimpleAvatar.prototype.OnScriptObjectDestroyed = function()
{
    if (framework.IsExiting())
        return; // Exiting, no need to do anything.
    // Must remember to manually disconnect subsystem signals, otherwise they'll continue to get signaled
    if (this.isServer)
    {
        frame.Updated.disconnect(this, this.ServerUpdate);
        scene.physics.Updated.disconnect(this, this.ServerUpdatePhysics);
    }
    else
    {
        frame.Updated.disconnect(this, this.ClientUpdate);
        if (this.inputContext)
            input.UnregisterInputContextRaw(this.inputContext.Name());
    }
}

SimpleAvatar.prototype.ServerInitialize = function() {
    // Create the avatar component & set the avatar appearance. The avatar component will create 
    // Mesh & AnimationController components, once the avatar asset has loaded.
    var avatar = this.me.GetOrCreateComponent("Avatar");
    
    // Try to dig login param avatar. This seemed like the easiest way to do it.
    // Parse the connection id from the entity name and get a connection for him,
    // check if a custom av url was passed when this client logged in.
    var entName = this.me.name;
    var indexNum = entName.substring(6); // 'Avatar' == 6, we want the number after that
    var clientPtr = server.GetUserConnection(parseInt(indexNum, 10));
    
    // Default avatar ref
    var avatarurl = "default_avatar.avatar";
    
    // This is done here (server side) because it seems changing the avatar 
    // appearance ref right after this in the client actually does not take effect at all.
    if (clientPtr != null)
    {
        var avatarurlProp = clientPtr.GetProperty("avatarurl");
        if (avatarurlProp && avatarurlProp.length > 0)
        {
            debug.Log("Avatar from login parameters enabled: " + avatarurlProp);
            avatarurl = avatarurlProp;
        }
    }
    
    var r = avatar.appearanceRef;
    r.ref = avatarurl;
    avatar.appearanceRef = r;

    // Create rigid body component and set physics properties
    var rigidbody = this.me.GetOrCreateComponent("RigidBody");
    var sizeVec = new float3(0.5, 2.4, 0.5);
    rigidbody.mass = this.avatarMass;
    rigidbody.shapeType = 3; // Capsule
    rigidbody.size = sizeVec;
    rigidbody.angularFactor = float3.zero; // Set zero angular factor so that body stays upright

    // Create dynamic component attributes for disabling/enabling functionality, and for camera distance / 1st/3rd mode
    var attrs = this.me.dynamiccomponent;
    attrs.CreateAttribute("bool", "enableWalk");
    attrs.CreateAttribute("bool", "enableJump");
    attrs.CreateAttribute("bool", "enableFly");
    attrs.CreateAttribute("bool", "enableRotate");
    attrs.CreateAttribute("bool", "enableAnimation");
    attrs.CreateAttribute("bool", "enableZoom");
    attrs.CreateAttribute("real", "cameraDistance");
    attrs.SetAttribute("enableWalk", true);
    attrs.SetAttribute("enableJump", true);
    attrs.SetAttribute("enableFly", true);
    attrs.SetAttribute("enableRotate", true);
    attrs.SetAttribute("enableAnimation", true);
    attrs.SetAttribute("enableZoom", true);
    attrs.SetAttribute("cameraDistance", 7.0);

    // Create an inactive proximity trigger, so that other proximity triggers can detect the avatar
    // var proxtrigger = me.GetOrCreateComponent("ProximityTrigger");
    // proxtrigger.active = false;

    // Hook to physics update
    scene.physics.Updated.connect(this, this.ServerUpdatePhysics);

    // Hook to tick update for animation update
    frame.Updated.connect(this, this.ServerUpdate);

    // Connect actions that come from the client.
    this.me.Action("Move").Triggered.connect(this, this.ServerHandleMove);
    this.me.Action("Stop").Triggered.connect(this, this.ServerHandleStop);
    this.me.Action("ToggleFly").Triggered.connect(this, this.ServerHandleToggleFly);
    this.me.Action("SetRotation").Triggered.connect(this, this.ServerHandleSetRotation);

    rigidbody.PhysicsCollision.connect(this, this.ServerHandleCollision);
}

SimpleAvatar.prototype.ServerUpdate = function(frametime) {
    var attrs = this.me.dynamiccomponent;

    if (!this.animsDetected)
        this.CommonFindAnimations();

    // If walk enable was toggled off, make sure the motion state is cleared
    if (!attrs.Attribute("enableWalk"))
    {
        this.motionX = 0;
        this.motionZ = 0;
    }
    
    // If flying enable was toggled off, but we are still flying, disable now
    if (this.flying && !attrs.Attribute("enableFly"))
        this.ServerSetFlying(false);

    this.CommonUpdateAnimation(frametime);
}

SimpleAvatar.prototype.ServerHandleCollision = function(ent, pos, normal, distance, impulse, newCollision) {
    if (this.falling && newCollision) {
        this.falling = false;
        this.ServerSetAnimationState();
    }
}

SimpleAvatar.prototype.ServerUpdatePhysics = function(frametime) {
    var placeable = this.me.placeable;
    var rigidbody = this.me.rigidbody;
    var attrs = this.me.dynamiccomponent;

    if (!this.flying) {
        // Apply motion force
        // If diagonal motion, normalize
        if (this.motionX != 0 || this.motionZ != 0) {
            var impulse = new float3(this.motionX, 0, -this.motionZ).Normalized().Mul(this.moveForce);
            var tm = placeable.LocalToWorld();
            impulse = tm.MulDir(impulse);
            rigidbody.ApplyImpulse(impulse);
        }

        // Apply jump
        if (this.motionY == 1 && !this.falling) {
            if (attrs.Attribute("enableJump")) {
                var jumpVec = new float3(0, 75, 0);
                this.motionY = 0;
                this.falling = true;
                rigidbody.ApplyImpulse(jumpVec);
            }
        }

        // Apply damping. Only do this if the body is active, because otherwise applying forces
        // to a resting object wakes it up
        if (rigidbody.IsActive()) {
            var dampingVec = rigidbody.GetLinearVelocity();
            dampingVec.x = -this.dampingForce * dampingVec.x;
            dampingVec.y = 0;
            dampingVec.z = -this.dampingForce * dampingVec.z;
            rigidbody.ApplyImpulse(dampingVec);
        }
    } else {
        // Manually move the avatar placeable when flying
        // this has the downside of no collisions.
        // Feel free to reimplement properly with mass enabled.
        var avTransform = placeable.transform;

        // Make a vector where we have moved
        var moveVec = new float3(this.motionX * this.flySpeedFactor, this.motionY * this.flySpeedFactor, -this.motionZ * this.flySpeedFactor);

        // Apply that with av looking direction to the current position
        var offsetVec = placeable.LocalToWorld().MulDir(moveVec);
        avTransform.pos.x = avTransform.pos.x + offsetVec.x;
        avTransform.pos.y = avTransform.pos.y + offsetVec.y;
        avTransform.pos.z = avTransform.pos.z + offsetVec.z;

        placeable.transform = avTransform;
    }
}

SimpleAvatar.prototype.ServerHandleMove = function(param) {
    var attrs = this.me.dynamiccomponent;
    if (attrs.Attribute("enableWalk")) {
        if (param == "forward")
            this.motionZ = 1;
        if (param == "back")
            this.motionZ = -1;
        if (param == "right")
            this.motionX = 1;
        if (param == "left")
            this.motionX = -1;
    }

    if (param == "up")
        this.motionY = 1;
    if (param == "down")
        this.motionY = -1;

    this.ServerSetAnimationState();
}

SimpleAvatar.prototype.ServerHandleStop = function(param) {
    if (param == "forward" && this.motionZ == 1)
        this.motionZ = 0;
    if (param == "back" && this.motionZ == -1)
        this.motionZ = 0;
    if (param == "right" && this.motionX == 1)
        this.motionX = 0;
    if (param == "left" && this.motionX == -1)
        this.motionX = 0;
    if (param == "up" && this.motionY == 1)
        this.motionY = 0;
    if (param == "down" && this.motionY == -1)
        this.motionY = 0;

    this.ServerSetAnimationState();
}

SimpleAvatar.prototype.ServerHandleToggleFly = function() {
    this.ServerSetFlying(!this.flying);
}

SimpleAvatar.prototype.ServerSetFlying = function(newFlying) {
    var attrs = this.me.dynamiccomponent;
    if (!attrs.Attribute("enableFly"))
        newFlying = false;

    if (this.flying == newFlying)
        return;

    var rigidbody = this.me.rigidbody;
    this.flying = newFlying;
    if (this.flying) {
        rigidbody.mass = 0;
    } else {
        var placeable = this.me.placeable;
        // Set mass back for collisions
        rigidbody.mass = this.avatarMass;
        // Push avatar a bit to the fly direction
        // so the motion does not just stop to a wall
        var moveVec = new float3(this.motionX * 120, this.motionY * 120, -this.motionZ * 120);
        var pushVec = placeable.LocalToWorld().MulDir(moveVec);
        rigidbody.ApplyImpulse(pushVec);
    }
    this.ServerSetAnimationState();
}

SimpleAvatar.prototype.ServerHandleSetRotation = function(param) {
    var attrs = this.me.dynamiccomponent;
    if (attrs.Attribute("enableRotate")) {
        var rot = new float3(0, parseFloat(param), 0);
        this.me.rigidbody.SetRotation(rot);
    }
}

SimpleAvatar.prototype.ServerSetAnimationState = function() {
    // Not flying: Stand, Walk or Crouch
    var animName = this.standAnimName;
    if (this.motionX != 0 || this.motionZ != 0)
        animName = this.walkAnimName;

    // Flying: Fly if moving forward or back, otherwise hover
    if (this.flying || this.falling) {
        animName = this.flyAnimName;
        if (this.motionZ == 0)
            animName = this.hoverAnimName;
    }

    if (animName == "")
        return;

    // Update the variable to sync to client if changed
    var animcontroller = this.me.animationcontroller;
    if (animcontroller != null) {
        if (animcontroller.animationState != animName) {
            animcontroller.animationState = animName;
        }
    }
}

SimpleAvatar.prototype.ClientInitialize = function() {
    // Set all avatar entities as temprary also on the clients.
    // This is already done in the server but the info seems to not travel to the clients!
    this.me.SetTemporary(true);
    
    // Check if this is our own avatar
    // Note: bad security. For now there's no checking who is allowed to invoke actions
    // on an entity, and we could theoretically control anyone's avatar
    if (this.me.name == "Avatar" + client.connectionId) {
        this.ownAvatar = true;
        this.ClientCreateInputContext();
        this.ClientCreateAvatarCamera();
        if (!framework.IsHeadless())
            this.crosshair = new Crosshair(/*bool useLabelInsteadOfCursor*/ true);
        var soundlistener = this.me.GetOrCreateComponent("SoundListener", 2, false);
        soundlistener.active = true;
    }
    else
    {
        // Make hovering name tag for other clients
        var clientName = this.me.Component("Name");
        if (clientName != null) {
            // Description holds the actual login name
            if (clientName.description != "") {
                var nameTag = this.me.GetOrCreateComponent("HoveringText", 2, false);
                if (nameTag != null) {
                    var texWidth = clientName.description.length * 50;
                    if (texWidth < 256)
                        texWidth = 256;
                    if (texWidth > 1024)
                        texWidth = 1024;
                    nameTag.texWidth = texWidth;
                    nameTag.width = texWidth / 256.0;

                    nameTag.SetTemporary(true);
                    nameTag.text = clientName.description;

                    var pos = nameTag.position;
                    pos.y = 1.3;
                    nameTag.position = pos;
                    nameTag.fontSize = 60;

                    var color = new Color(0.2, 0.2, 0.2, 1.0);
                    nameTag.backgroundColor = color;
                    var font_color = new Color(1.0, 1.0, 1.0, 1.0);
                    nameTag.fontColor = font_color;
                }
            }
        }
    }

    // Hook to tick update to update visual effects (both own and others' avatars)
    frame.Updated.connect(this, this.ClientUpdate);
}

SimpleAvatar.prototype.IsCameraActive = function()
{
    var cameraEntity = scene.EntityByName("AvatarCamera"); // TODO EntityById
    if (cameraEntity == null || cameraEntity.camera == null)
        return false;
    return cameraEntity.camera.IsActive();
}

SimpleAvatar.prototype.ClientUpdate = function(frametime)
{
    if (this.ownAvatar)
    {
        this.ClientUpdateRotation(frametime);
        this.ClientUpdateAvatarCamera(frametime);
    }

    if (!this.animsDetected)
        this.CommonFindAnimations();

    this.CommonUpdateAnimation(frametime);
}

SimpleAvatar.prototype.ClientCreateInputContext = function()
{
    this.inputContext = input.RegisterInputContextRaw("Avatar" + client.connectionId, 101);
    this.inputContext.takeMouseEventsOverQt = true;
    this.inputContext.KeyEventReceived.connect(this, this.ClientHandleKeyEvent);
    this.inputContext.MouseMove.connect(this, this.ClientHandleMouseMove);
    // Mouse cursor visibility altering on RMB press and release
    var self = this;
    this.inputContext.MouseRightPressed.connect(function() { if (self.IsCameraActive()) input.SetMouseCursorVisible(false); });
    this.inputContext.MouseRightReleased.connect(function() { if (self.IsCameraActive()) input.SetMouseCursorVisible(true); });
    this.inputContext.MouseScroll.connect(this, this.ClientHandleMouseScrollEvent);
    this.inputContext.GestureStarted.connect(this, this.GestureStarted);
    this.inputContext.GestureUpdated.connect(this, this.GestureUpdated);
}

SimpleAvatar.prototype.ClientHandleKeyEvent = function(e)
{
    if (!this.IsCameraActive())
        return;

    switch(e.keyCode)
    {
        // Server-side actions:
    case Qt.Key_W:
        if (e.eventType == KeyEvent.KeyPressed) this.me.Exec(EntityAction.Server, "Move", "forward");
        else if (e.eventType == KeyEvent.KeyReleased) this.me.Exec(EntityAction.Server, "Stop", "forward");
        break;
    case Qt.Key_S:
        if (e.eventType == KeyEvent.KeyPressed) this.me.Exec(EntityAction.Server, "Move", "back");
        else if (e.eventType == KeyEvent.KeyReleased) this.me.Exec(EntityAction.Server, "Stop", "back");
        break;
    case Qt.Key_A:
        if (e.eventType == KeyEvent.KeyPressed) this.me.Exec(EntityAction.Server, "Move", "left");
        else if (e.eventType == KeyEvent.KeyReleased) this.me.Exec(EntityAction.Server, "Stop", "left");
        break;
    case Qt.Key_D:
        if (e.eventType == KeyEvent.KeyPressed) this.me.Exec(EntityAction.Server, "Move", "right");
        else if (e.eventType == KeyEvent.KeyReleased) this.me.Exec(EntityAction.Server, "Stop", "right");
        break;
    case Qt.Key_Up:
        if (e.eventType == KeyEvent.KeyPressed) this.me.Exec(EntityAction.Server, "Move", "forward");
        else if (e.eventType == KeyEvent.KeyReleased) this.me.Exec(EntityAction.Server, "Stop", "forward");
        break;
    case Qt.Key_Down:
        if (e.eventType == KeyEvent.KeyPressed) this.me.Exec(EntityAction.Server, "Move", "back");
        else if (e.eventType == KeyEvent.KeyReleased) this.me.Exec(EntityAction.Server, "Stop", "back");
        break;
    case Qt.Key_F:
        if (e.eventType == KeyEvent.KeyPressed) this.me.Exec(EntityAction.Server, "ToggleFly");
        break;
    case Qt.Key_Space:
        if (e.eventType == KeyEvent.KeyPressed) this.me.Exec(EntityAction.Server, "Move", "up");
        else if (e.eventType == KeyEvent.KeyReleased) this.me.Exec(EntityAction.Server, "Stop", "up");
        break;
    case Qt.Key_C:
        if (e.eventType == KeyEvent.KeyPressed) this.me.Exec(EntityAction.Server, "Move", "down");
        else if (e.eventType == KeyEvent.KeyReleased) this.me.Exec(EntityAction.Server, "Stop", "down");
        break;
        // Client-side actions:
    case Qt.Key_Plus:
        if (e.eventType == KeyEvent.KeyPressed) this.ClientHandleKeyboardZoom("in");
        break;
    case Qt.Key_Minus:
        if (e.eventType == KeyEvent.KeyPressed) this.ClientHandleKeyboardZoom("out");
        break;
    case Qt.Key_Left:
        if (e.eventType == KeyEvent.KeyPressed) this.ClientHandleRotate("left");
        else if (e.eventType == KeyEvent.KeyReleased) this.ClientHandleStopRotate("left");
        break;
    case Qt.Key_Right:
        if (e.eventType == KeyEvent.KeyPressed) this.ClientHandleRotate("right");
        else if (e.eventType == KeyEvent.KeyReleased) this.ClientHandleStopRotate("right");
        break;
    }
}

SimpleAvatar.prototype.ClientCreateAvatarCamera = function() {
    var cameraentity = scene.EntityByName("AvatarCamera");
    if (cameraentity == null)
    {
        cameraentity = scene.CreateLocalEntity();
        cameraentity.SetName("AvatarCamera");
        cameraentity.SetTemporary(true);
    }

    var camera = cameraentity.GetOrCreateComponent("Camera");
    var placeable = cameraentity.GetOrCreateComponent("Placeable");

    camera.SetActive();
    
    var parentRef = placeable.parentRef;
    parentRef.ref = this.me; // Parent camera to avatar, always
    placeable.parentRef = parentRef;

    // Set initial position
    this.ClientUpdateAvatarCamera();
}

SimpleAvatar.prototype.GestureStarted = function(gestureEvent) {
    if (!this.IsCameraActive())
        return;
    if (gestureEvent.gestureType == Qt.PanGesture)
    {
        this.listenGesture = true;

        var attrs = this.me.dynamiccomponent;
        if (attrs.Attribute("enableRotate")) {
            this.yaw += gestureEvent.Gesture().offset.toPoint().x();
            this.me.Exec(EntityAction.Server, "SetRotation", this.yaw.toString());
        }

        gestureEvent.Accept();
    }
    else if (gestureEvent.gestureType == Qt.PinchGesture)
        gestureEvent.Accept();
}

SimpleAvatar.prototype.GestureUpdated = function(gestureEvent) {
    if (!this.IsCameraActive())
        return;

    if (gestureEvent.gestureType == Qt.PanGesture && this.listenGesture == true)
    {
        // Rotate avatar with X pan gesture
        delta = gestureEvent.Gesture().delta.toPoint();
        this.yaw += delta.x;
        this.me.Exec(EntityAction.Server, "SetRotation", this.yaw.toString());

        // Start walking or stop if total Y len of pan gesture is 100
        var walking = false;
        if (this.me.animationcontroller.animationState == this.walkAnimName)
            walking = true;
        var totalOffset = gestureEvent.Gesture().offset.toPoint();
        if (totalOffset.y() < -100)
        {
            if (walking) {
                this.me.Exec(EntityAction.Server, "Stop", "forward");
                this.me.Exec(EntityAction.Server, "Stop", "back");
            } else 
                this.me.Exec(EntityAction.Server, "Move", "forward");
            listenGesture = false;
        }
        else if (totalOffset.y() > 100)
        {
            if (walking) {
                this.me.Exec(EntityAction.Server, "Stop", "forward");
                this.me.Exec(EntityAction.Server, "Stop", "back");
            } else
                this.me.Exec(EntityAction.Server, "Move", "back");
            this.listenGesture = false;
        }
        gestureEvent.Accept();
    }
    else if (gestureEvent.gestureType == Qt.PinchGesture)
    {
        var scaleChange = gestureEvent.Gesture().scaleFactor - gestureEvent.Gesture().lastScaleFactor;
        if (scaleChange > 0.1 || scaleChange < -0.1)
            this.ClientZoom(scaleChange * 100);
        gestureEvent.Accept();
    }
}

SimpleAvatar.prototype.ClientHandleKeyboardZoom = function(direction) {
    if (direction == "in")
        this.ClientZoom(10);
    else if (direction == "out")
        this.ClientZoom(-10);
}

SimpleAvatar.prototype.ClientHandleMouseScrollEvent = function(e)
{
    if (this.IsCameraActive())
        this.ClientZoom(e.relativeZ);
}

SimpleAvatar.prototype.ClientZoom = function(relativeScroll) {
    var attrs = this.me.dynamiccomponent;
    // Check that zoom is allowed
    if (!attrs.Attribute("enableZoom"))
        return;

    var avatarCameraDistance = attrs.Attribute("cameraDistance");

    if (!this.IsCameraActive())
        return;

    var moveAmount = 0;
    if (relativeScroll < 0 && avatarCameraDistance < 500) {
        if (relativeScroll < -50)
            moveAmount = 2;
        else
            moveAmount = 1;
    } else if (relativeScroll > 0 && avatarCameraDistance > 0) {
        if (relativeScroll > 50)
            moveAmount = -2
        else
            moveAmount = -1;
    }
    if (moveAmount != 0)
    {
        // Add movement
        avatarCameraDistance = avatarCameraDistance + moveAmount;
        // Clamp distance to be between -0.5 and 500
        if (avatarCameraDistance < -0.5)
            avatarCameraDistance = -0.5;
        else if (avatarCameraDistance > 500)
            avatarCameraDistance = 500;

        attrs.SetAttribute("cameraDistance", avatarCameraDistance);
    }
}

SimpleAvatar.prototype.ClientHandleRotate = function(param) {
    if (param == "left")
        this.rotate = -1;
    if (param == "right")
        this.rotate = 1;
}

SimpleAvatar.prototype.ClientHandleStopRotate = function(param) {
    if (param == "left" && this.rotate == -1)
        this.rotate = 0;
    if (param == "right" && this.rotate == 1)
        this.rotate = 0;
}

SimpleAvatar.prototype.ClientUpdateRotation = function(frametime) {
    var attrs = this.me.dynamiccomponent;
    // Check that rotation is allowed
    if (!attrs.Attribute("enableRotate"))
        return;

    if (this.rotate != 0) {
        this.yaw -= this.rotateSpeed * this.rotate * frametime;
        this.me.Exec(EntityAction.Server, "SetRotation", this.yaw.toString());
    }
}

SimpleAvatar.prototype.ClientUpdateAvatarCamera = function() {

    // Check 1st/3rd person mode toggle
    this.ClientCheckState();

    var attrs = this.me.dynamiccomponent;
    if (attrs == null)
        return;
    var avatarCameraDistance = attrs.Attribute("cameraDistance");
    var firstPerson = avatarCameraDistance < 0;

    var cameraentity = scene.EntityByName("AvatarCamera"); // TODO EntityById
    if (cameraentity == null)
        return;
    var cameraplaceable = cameraentity.placeable;

    var cameratransform = cameraplaceable.transform;
    cameratransform.rot = new float3(this.pitch, 0, 0);
    cameratransform.pos = new float3(0, this.avatarCameraHeight, avatarCameraDistance);

    // Track the head bone in 1st person
    if (firstPerson && this.me.mesh)
    {
        this.me.mesh.ForceSkeletonUpdate();
        var headPos = this.me.mesh.GetBoneDerivedPosition("Bip01_Head");
        headPos.z -= 0.5;
        headPos.y -= 0.7;
        cameratransform.pos = headPos;
    }

    cameraplaceable.transform = cameratransform;
}

SimpleAvatar.prototype.ClientCheckState = function() {
    var attrs = this.me.dynamiccomponent;
    var firstPerson = attrs.Attribute("cameraDistance") < 0;

    var cameraentity = scene.EntityByName("AvatarCamera");
    var avatarPlaceable = this.me.Component("Placeable");

    if (this.crosshair == null)
        return;
    // If entity got destroyed or something fatal, return cursor
    if (cameraentity == null || avatarPlaceable == null) {
        if (this.crosshair.isActive()) {
            this.crosshair.hide();
        }
        return;
    }

    if (!firstPerson) {
        if (this.crosshair.isActive()) {
            this.crosshair.hide();
        }
        return;
    }
    else
    {
        // We might be in 1st person mode but camera might not be active
        // hide curson and show av
        if (!cameraentity.camera.IsActive()) {
            if (this.crosshair.isActive()) {
                this.crosshair.hide();
            }
        }
        else {
            // 1st person mode and camera is active
            // show curson and av
            if (!this.crosshair.isActive()) {
                this.crosshair.show();
            }
        }
    }
}

SimpleAvatar.prototype.ClientHandleMouseMove = function(mouseevent) {
    var attrs = this.me.dynamiccomponent;
    var firstPerson = attrs.Attribute("cameraDistance") < 0;

    if (firstPerson && input.IsMouseCursorVisible())
    {
        input.SetMouseCursorVisible(false);
        if (!this.crosshair.isUsingLabel)
            QApplication.setOverrideCursor(crosshair.cursor);
    }

    // Do not rotate if not allowed
    if (!attrs.Attribute("enableRotate"))
        return;

    // Do not rotate in third person if right mouse button not held down
    if (!firstPerson && input.IsMouseCursorVisible())
        return;

    if (mouseevent.IsRightButtonDown())
        this.LockMouseMove(mouseevent.relativeX, mouseevent.relativeY);
    else
        this.isMouseLookLockedOnX = true;

    var cameraentity = scene.EntityByName("AvatarCamera"); // TODO Should be optimized (EntityById or store camera entity to SimpleAvatar)
    if (cameraentity == null)
        return;

    // Don't move av rotation if we are not the active cam
    if (!cameraentity.camera.IsActive())
        return;

    if (mouseevent.relativeX != 0)
    {
        // Rotate avatar or camera
        this.yaw -= this.mouseRotateSensitivity * parseInt(mouseevent.relativeX);
        this.me.Exec(EntityAction.Server, "SetRotation", this.yaw.toString());
    }

    if (mouseevent.relativeY != 0 && (firstPerson || !this.isMouseLookLockedOnX))
    {
        // Look up/down
        var attrs = this.me.dynamiccomponent;
        this.pitch -= this.mouseRotateSensitivity * parseInt(mouseevent.relativeY);

        // Don't let the 1st person flip vertically, 180 deg view angle
        if (this.pitch < -90)
            this.pitch = -90;
        if (this.pitch > 90)
            this.pitch = 90;
    }
}

SimpleAvatar.prototype.LockMouseMove = function(x,y) {
    if (Math.abs(y) > Math.abs(x))
        this.isMouseLookLockedOnX = false;
    else
        this.isMouseLookLockedOnX = true;
}

SimpleAvatar.prototype.CommonFindAnimations = function() {
    var animcontrol = this.me.animationcontroller;
    if (animcontrol == null)
        return;
    var availableAnimations = animcontrol.GetAvailableAnimations();
    if (availableAnimations.length > 0) {
        // Detect animation names
        for(var i=0; i<this.animList.length; i++) {
            var animName = this.animList[i];
            if (availableAnimations.indexOf(animName) == -1) {
                // Disable this animation by setting it to a empty string
                print("Could not find animation for:", animName, " - disabling animation");
                this.animList[i] = "";
            }
        }

        // Assign the possible empty strings for
        // not found anims back to the variables
        this.standAnimName = this.animList[0];
        this.walkAnimName = this.animList[1];
        this.flyAnimName = this.animList[2];
        this.hoverAnimName = this.animList[3];

        this.animsDetected = true;
    }
}

SimpleAvatar.prototype.CommonUpdateAnimation = function(frametime) {
    // This function controls the known move animations, such as walk, fly, hover and stand,
    // which are replicated through the animationState attribute of the AnimationController.
    // Only one such move animation can be active at a time.
    // Other animations, such as for gestures, can be freely enabled/disabled by other scripts.

    var attrs = this.me.dynamiccomponent;

    if (!this.animsDetected)
        return;

    var animcontroller = this.me.animationcontroller;
    var rigidbody = this.me.rigidbody;
    if (animcontroller == null || rigidbody == null)
        return;

    if (!attrs.Attribute("enableAnimation"))
    {
        // When animations disabled, forcibly disable all running move animations
        // Todo: what if custom scripts want to run the move anims as well?
        for (var i = 0; i < this.animList.length; ++i) {
            if (this.animList[i] != "")
                animcontroller.DisableAnimation(this.animList[i], 0.25);
        }
        return;
    }

    var animName = animcontroller.animationState;

    // Enable animation, skip with headless server
    if (animName != "" && !framework.IsHeadless()) {
        // Do custom speeds for certain anims
        if (animName == this.hoverAnimName) {
            animcontroller.SetAnimationSpeed(animName, 0.25);
        }
        // Enable animation
        if (!animcontroller.IsAnimationActive(animName)) {
            animcontroller.EnableAnimation(animName, true, 0.25, false);
        }
        // Disable other move animations
        for (var i = 0; i < this.animList.length; ++i) {
            if ((this.animList[i] != animName) && (this.animList[i] != "") && (animcontroller.IsAnimationActive(this.animList[i])))
                animcontroller.DisableAnimation(this.animList[i], 0.25);
        }
    }

    // If walk animation is playing, adjust its speed according to the avatar's rigid body velocity
    if (animName != ""  && animcontroller.IsAnimationActive(this.walkAnimName)) {
        var velocity = rigidbody.linearVelocity;
        var walkspeed = Math.sqrt(velocity.x * velocity.x + velocity.z * velocity.z) * this.walkAnimSpeed;
        animcontroller.SetAnimationSpeed(this.walkAnimName, walkspeed);
    }
}
