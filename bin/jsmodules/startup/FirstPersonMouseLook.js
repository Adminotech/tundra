// NOTE This script is deprecated and not included in the startup by default.
// TODO Remove this for good.

var inputContext = input.RegisterInputContextRaw("fpsMouseLook", 150);
inputContext.SetTakeMouseEventsOverQt(true);
inputContext.MouseEventReceived.connect(MouseEvent);

// Returns user's avatar entity if connected.
function FindUserAvatar()
{
    var scene = framework.Scene().MainCameraScene();
    if (scene && client.IsConnected())
        return scene.EntityByName("Avatar" + client.connectionId);
    else
        return null;
}

// Hides and shows mouse cursor when RMB is pressed and released
function MouseEvent(event)
{
    // Never show mouse cursor, if avatar camera is active and in first-person mode.
    var avatarCameraActiveInFps = false;
    var userAvatar = FindUserAvatar();
    var scene = framework.Scene().MainCameraScene();
    if (userAvatar && scene)
    {
        var avatarCamera = scene.EntityByName("AvatarCamera");
        if (avatarCamera && avatarCamera.camera)
            avatarCameraActiveInFps = avatarCamera.camera.IsActive() && userAvatar.dynamicComponent.Attribute("cameraDistance") < 0;
    }

    if (event.eventType == MouseEvent.MousePressed && event.button == MouseEvent.RightButton &&
        input.IsMouseCursorVisible())
    {
        input.SetMouseCursorVisible(false);
    }
    else if (event.eventType == MouseEvent.MouseReleased && event.button == MouseEvent.RightButton &&
        !input.IsMouseCursorVisible() && !avatarCameraActiveInFps)
    {
        input.SetMouseCursorVisible(true);
    }
}
