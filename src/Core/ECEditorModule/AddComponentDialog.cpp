// For conditions of distribution and use, see copyright notice in LICENSE

#include "StableHeaders.h"
#include "DebugOperatorNew.h"

#include "AddComponentDialog.h"
#include "Framework.h"
#include "Scene/Scene.h"
#include "SceneAPI.h"
#include "Entity.h"

#include <QLayout>
#include <QLineEdit>
#include <QLabel>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <QGridLayout>

#include "MemoryLeakCheck.h"

AddComponentDialog::AddComponentDialog(Framework *fw, const QList<entity_id_t> &ids, QWidget *parent, Qt::WindowFlags f):
    QDialog(parent, f),
    framework_(fw),
    entities_(ids),
    name_line_edit_(0),
    type_combo_box_(0),
    ok_button_(0),
    cancel_button_(0)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowModality(Qt::WindowModal);
    setWindowTitle(tr("Add New Component"));
    if (graphicsProxyWidget())
        graphicsProxyWidget()->setWindowTitle(windowTitle());

    // Create widgets
    QLabel *component_type_label = new QLabel(tr("Component"), this);
    QLabel *component_name_label = new QLabel(tr("Name"), this);
    QLabel *component_sync_label = new QLabel(tr("Local"), this);
    QLabel *component_temp_label = new QLabel(tr("Temporary"), this);
    component_temp_label->setMinimumWidth(70);

    errorLabel = new QLabel(this);
    errorLabel->setStyleSheet("QLabel { background-color: rgba(255,0,0,150); padding: 4px; border: 1px solid grey; }");
    errorLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    errorLabel->setAlignment(Qt::AlignCenter);
    errorLabel->hide();

    name_line_edit_ = new QLineEdit(this);
    name_line_edit_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    type_combo_box_ = new QComboBox(this);
    type_combo_box_->setFocus(Qt::ActiveWindowFocusReason);
    type_combo_box_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    
    sync_check_box_ = new QCheckBox(this);
    sync_check_box_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    
    temp_check_box_ = new QCheckBox(this);
    temp_check_box_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    ok_button_ = new QPushButton(tr("Add"), this);
    ok_button_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    ok_button_->setDefault(true);
    
    cancel_button_ = new QPushButton(tr("Cancel"), this);
    cancel_button_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    cancel_button_->setAutoDefault(false);

    // Layouts
    QGridLayout *grid = new QGridLayout();
    grid->setVerticalSpacing(8);
    grid->addWidget(component_type_label, 0, 0);
    grid->addWidget(type_combo_box_, 0, 1, Qt::AlignLeft, 1);
    grid->addWidget(component_name_label, 1, 0);
    grid->addWidget(name_line_edit_, 1, 1, Qt::AlignLeft, 1);
    grid->addWidget(component_sync_label, 2, 0);
    grid->addWidget(sync_check_box_, 2, 1);
    grid->addWidget(component_temp_label, 3, 0);
    grid->addWidget(temp_check_box_, 3, 1);

    QHBoxLayout *buttons_layout = new QHBoxLayout();
    buttons_layout->addWidget(ok_button_);
    buttons_layout->addWidget(cancel_button_);

    QVBoxLayout *vertLayout = new QVBoxLayout();

    if (entities_.size() > 1)
    {
        QLabel *labelCompCount = new QLabel(tr("Adding component to %1 selected entities").arg(entities_.size()), this);
        labelCompCount->setStyleSheet("QLabel { background-color: rgba(230,230,230,255); padding: 4px; border: 1px solid grey; }");
        labelCompCount->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        labelCompCount->setAlignment(Qt::AlignCenter);
        vertLayout->addWidget(labelCompCount);
    }

    componentSelection_ = new ComponentMultiSelectWidget(
        QStringList() << "Placeable" << "Mesh" << "RigidBody" 
                      << "Script" << "DynamicComponent" << "Light", 3, this);
    componentSelection_->mainLayout->insertWidget(0, new QLabel("<b>Quick Selection</b>", componentSelection_));
    componentSelection_->mainLayout->setContentsMargins(0, 15, 0, 15);
    componentSelection_->mainLayout->setSpacing(7);
    componentSelection_->InspectTargets(framework_->Scene()->MainCameraScene(), ids);
    
    // Disable verifying component name if more than one component is selected. In this case it will let you check
    // any button, later on if there is same named component conflict, it just wont be made. Note that the InspectTargets
    // will disable multiselecting components that do already exist.
    connect(componentSelection_, SIGNAL(NumComponentsSelectedChanged(int)), SLOT(OnNumComponentsSelectedChanged(int))),

    vertLayout->addLayout(grid);
    vertLayout->addWidget(componentSelection_);
    vertLayout->addSpacerItem(new QSpacerItem(1,1, QSizePolicy::Fixed, QSizePolicy::Expanding));
    vertLayout->addWidget(errorLabel);
    vertLayout->addLayout(buttons_layout);

    setLayout(vertLayout);
    resize(350, width());

    // Connect signals
    connect(name_line_edit_, SIGNAL(textChanged(const QString&)), this, SLOT(CheckComponentName()));
    connect(type_combo_box_, SIGNAL(currentIndexChanged(const QString&)), this, SLOT(ComponentSelectionChanged()));
    connect(ok_button_, SIGNAL(clicked()), this, SLOT(accept()));
    connect(cancel_button_, SIGNAL(clicked()), this, SLOT(reject()));
    connect(sync_check_box_, SIGNAL(toggled(bool)), this, SLOT(CheckTempAndSync()));
    connect(temp_check_box_, SIGNAL(toggled(bool)), this, SLOT(CheckTempAndSync()));

    CheckTempAndSync();
}

AddComponentDialog::~AddComponentDialog()
{
}

void AddComponentDialog::SetComponentList(const QStringList &component_types)
{
    foreach(const QString &type, component_types)
        type_combo_box_->addItem(IComponent::EnsureTypeNameWithoutPrefix(type));
}

void AddComponentDialog::SetComponentName(const QString &name)
{
    name_line_edit_->setText(name);
}

QString AddComponentDialog::TypeName() const
{
    return IComponent::EnsureTypeNameWithPrefix(type_combo_box_->currentText());
}

QList<u32> AddComponentDialog::TypeIds() const
{
    QList<u32> typeIds;
    if (type_combo_box_->isEnabled())
        typeIds << framework_->Scene()->ComponentTypeIdForTypeName(type_combo_box_->currentText());
    foreach(const QString &typeName, componentSelection_->SelectedComponents())
    {
        u32 typeId = framework_->Scene()->ComponentTypeIdForTypeName(typeName);
        if (!typeIds.contains(typeId))
            typeIds << typeId;
    }
    return typeIds;
}

QString AddComponentDialog::Name() const
{
    if (name_line_edit_->isEnabled())
        return name_line_edit_->text();
    return "";
}

bool AddComponentDialog::IsReplicated() const
{
    return !sync_check_box_->isChecked();
}

bool AddComponentDialog::IsTemporary() const
{
    return temp_check_box_->isChecked();
}

QList<entity_id_t> AddComponentDialog::EntityIds() const
{
    return entities_;
}

void AddComponentDialog::CheckComponentName()
{
    Scene *scene = framework_->Scene()->MainCameraScene();
    if (!scene)
        return;

    QString typeName = IComponent::EnsureTypeNameWithPrefix(type_combo_box_->currentText());
    QString compName = (name_line_edit_->isEnabled() ? name_line_edit_->text().trimmed() : "");

    bool nameDuplicates = false;
    if (name_line_edit_->isEnabled())
    {
        for(uint i = 0; i < (uint)entities_.size(); i++)
        {
            EntityPtr entity = scene->EntityById(entities_[i]);
            if (!entity)
                continue;
            if (entity->Component(typeName, compName))
            {
                nameDuplicates = true;
                break;
            }
        }
    }

    QString errorText = "";
    if (nameDuplicates)
    {
        errorText += type_combo_box_->currentText() + tr(" component with name ");
        errorText += compName.isEmpty() ? "<no name>" : "\"" + compName + "\"";
        errorText += tr(" already exists. Pick a unique name.");
    }

    ok_button_->setDisabled(nameDuplicates);
    errorLabel->setVisible(!errorText.isEmpty());
    errorLabel->setText(errorText);
    layout()->update();
}

void AddComponentDialog::ComponentSelectionChanged()
{
    CheckComponentName();

    QString typeName = IComponent::EnsureTypeNameWithPrefix(type_combo_box_->currentText());
    componentSelection_->SetSelected(typeName, true, true);
}

void AddComponentDialog::CheckTempAndSync()
{
    sync_check_box_->setText(sync_check_box_->isChecked() ? tr("Creating as Local") : tr("Creating as Replicated"));
    temp_check_box_->setText(temp_check_box_->isChecked() ? tr("Creating as Temporary") : " ");

    sync_check_box_->setStyleSheet(sync_check_box_->isChecked() ? "color: blue;" : "color: black;");
    temp_check_box_->setStyleSheet(temp_check_box_->isChecked() ? "color: red;" : "color: black;");
}

void AddComponentDialog::OnNumComponentsSelectedChanged(int num)
{
    bool multiSelection = (num > 1);
    type_combo_box_->setDisabled(multiSelection);
    name_line_edit_->setDisabled(multiSelection);
    if (multiSelection)
    {
        ok_button_->setDisabled(false);
        errorLabel->setVisible(false);
        layout()->update();
    }
    if (multiSelection || num == 1)
    {
        int index = type_combo_box_->findText(componentSelection_->SelectedComponents().first());
        if (index != -1 && type_combo_box_->currentIndex() != index)
            type_combo_box_->setCurrentIndex(index);
    }
}

void AddComponentDialog::hideEvent(QHideEvent * /*event*/)
{
    deleteLater();
}

// ComponentMultiSelectWidget

ComponentMultiSelectWidget::ComponentMultiSelectWidget(const QStringList &componentTypeNames, int gridWidth, QWidget *parent) :
    QWidget(parent)
{
    setObjectName("ComponentMultiSelectWidget");

    mainLayout = new QVBoxLayout();
    mainLayout->setContentsMargins(0,0,0,0);

    QGridLayout *grid = new QGridLayout();

    if (gridWidth < 1)
        gridWidth = 1;

    QStringList added;
    int row = 0;
    int column = 0;
    foreach(QString componentTypeName, componentTypeNames)
    {
        const QString simplified = IComponent::EnsureTypeNameWithoutPrefix(componentTypeName.trimmed());
        if (added.contains(simplified, Qt::CaseInsensitive))
            continue;

        QCheckBox *checkBox = new QCheckBox(simplified, this);
        checkBox->setProperty("typeName", simplified);
        connect(checkBox, SIGNAL(toggled(bool)), SLOT(OnCheckboxToggled(bool)));
        
        selectionCheckBoxes_ << checkBox;
        added << simplified;

        grid->addWidget(checkBox, row, column);

        column++;
        if (column >= gridWidth)
        {
            row++;
            column = 0;
        }
    }

    mainLayout->addLayout(grid);
    setLayout(mainLayout);
}

void ComponentMultiSelectWidget::InspectTargets(const Scene *scene, const QList<entity_id_t> entities)
{
    if (!scene)
        return;

    foreach(QCheckBox *checkBox, selectionCheckBoxes_)
    {
        QString typeName = checkBox->property("typeName").toString();

        bool allHaveIt = true;
        foreach(entity_id_t entId, entities)
        {
            EntityPtr entity = scene->EntityById(entId);
            if (!entity)
                continue;
            if (!entity->Component(typeName))
            {
                allHaveIt = false;
                break;
            }
        }
        if (allHaveIt)
        {
            checkBox->setChecked(false);
            checkBox->setDisabled(true);
        }
    }
}

void ComponentMultiSelectWidget::SetSelected(const QString &componentTypeName, bool selected, bool revertLastSelection)
{
    const QString simplified = IComponent::EnsureTypeNameWithoutPrefix(componentTypeName.trimmed());

    if (revertLastSelection && !lastSelected_.isEmpty() && lastSelected_.compare(simplified, Qt::CaseInsensitive) != 0)
        RevertLastSelection();

    // If a single item is checked and we receive a change from the combo box. We want to uncheck the checkbox ui selection.
    if (NumSelectedComponents() == 1 && !checkBoxSelected_.isEmpty() && checkBoxSelected_.compare(simplified, Qt::CaseInsensitive) != 0)
    {
        QCheckBox *checkBox = CheckBox(checkBoxSelected_);
        checkBoxSelected_ = "";
        if (checkBox && checkBox->isChecked())
            checkBox->setChecked(false);
    }

    QCheckBox *checkBox = CheckBox(simplified);
    if (!checkBox || !checkBox->isEnabled() || checkBox->isChecked() == selected)
        return;
    
    lastSelected_ = simplified;
    checkBox->setChecked(selected);
}

void ComponentMultiSelectWidget::RevertLastSelection()
{
    QCheckBox *checkBox = CheckBox(lastSelected_);
    if (!checkBox)
        return;
    lastSelected_ = "";

    if (checkBox->isEnabled())
        checkBox->setChecked(!checkBox->isChecked());
    else
        checkBox->setChecked(false);    
}

QStringList ComponentMultiSelectWidget::SelectedComponents() const
{
    QStringList result;
    foreach(QCheckBox *checkBox, selectionCheckBoxes_)
    {
        if (checkBox->isEnabled() && checkBox->isChecked())
            result << checkBox->property("typeName").toString();
    }
    return result;
}

void ComponentMultiSelectWidget::OnCheckboxToggled(bool checked)
{
    QCheckBox *checkBox = qobject_cast<QCheckBox*>(sender());
    if (!checkBox)
        return;

    QString typeName = checkBox->property("typeName").toString();
    int num = NumSelectedComponents();
    checkBoxSelected_ = (num == 1 ? SelectedComponents().first() : "");

    emit ComponentSelectionChanged(typeName, checked);
    emit NumComponentsSelectedChanged(num);
}

int ComponentMultiSelectWidget::NumSelectedComponents() const
{
    int num = 0;
    foreach(QCheckBox *checkBox, selectionCheckBoxes_)
    {
        if (checkBox->isEnabled() && checkBox->isChecked())
            num++;
    }
    return num;
}

QCheckBox *ComponentMultiSelectWidget::CheckBox(const QString &componentTypeName)
{
    if (componentTypeName.isEmpty())
        return 0;
    foreach(QCheckBox *checkBox, selectionCheckBoxes_)
    {
        if (checkBox->property("typeName").toString().compare(componentTypeName, Qt::CaseInsensitive) == 0)
            return checkBox;
    }
    return 0;
}