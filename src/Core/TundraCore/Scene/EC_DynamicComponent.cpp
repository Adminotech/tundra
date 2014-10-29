// For conditions of distribution and use, see copyright notice in LICENSE

#include "StableHeaders.h"
#include "DebugOperatorNew.h"

#include "EC_DynamicComponent.h"
#include "SceneAPI.h"
#include "Entity.h"
#include "LoggingFunctions.h"
#include "Scene/Scene.h"

#include <kNet.h>

#include <QDomDocument>

#include "MemoryLeakCheck.h"

/** @cond PRIVATE */
/// Function that is used by std::sort algorithm to sort attributes by their ID.
bool CmpAttributeById(const IAttribute *a, const IAttribute *b)
{
    return a->Id().compare(b->Id(), Qt::CaseInsensitive) < 0;
}

/// Function that is used by std::sort algorithm to sort AttributeDesc by their ID.
bool CmpAttributeDataById(const AttributeDesc &a, const AttributeDesc &b)
{
    return a.id.compare(b.id, Qt::CaseInsensitive) < 0;
}

/** @endcond */

EC_DynamicComponent::EC_DynamicComponent(Scene* scene):
    IComponent(scene)
{
}

EC_DynamicComponent::~EC_DynamicComponent()
{
}

void EC_DynamicComponent::DeserializeFrom(QDomElement& element, AttributeChange::Type change)
{
    if (!BeginDeserialization(element))
        return;

    std::vector<AttributeDesc> deserializedAttributes;
    QDomElement child = element.firstChildElement("attribute");
    while(!child.isNull())
    {
        AttributeDesc attributeData;
        attributeData.id = child.attribute("id");
        attributeData.name = child.attribute("name");
        if (attributeData.id.isEmpty()) // Fallback if ID is not defined
            attributeData.id = attributeData.name;
        attributeData.typeName = child.attribute("type");
        attributeData.value = child.attribute("value");

        deserializedAttributes.push_back(attributeData);

        child = child.nextSiblingElement("attribute");
    }

    DeserializeCommon(deserializedAttributes, change);
}

void EC_DynamicComponent::DeserializeCommon(std::vector<AttributeDesc>& deserializedAttributes, AttributeChange::Type change)
{
    // Sort both lists in alphabetical order.
    AttributeVector oldAttributes = NonEmptyAttributes();
    std::stable_sort(oldAttributes.begin(), oldAttributes.end(), &CmpAttributeById);
    std::stable_sort(deserializedAttributes.begin(), deserializedAttributes.end(), &CmpAttributeDataById);

    std::vector<AttributeDesc> addAttributes;
    std::vector<QString> remAttributeIds;
    AttributeVector::iterator iter1 = oldAttributes.begin();
    std::vector<AttributeDesc>::iterator iter2 = deserializedAttributes.begin();

    // Check what attributes we need to add or remove from the dynamic component (done by comparing two list differences).
    while(iter1 != oldAttributes.end() || iter2 != deserializedAttributes.end())
    {
        // No point to continue the iteration if other list is empty. We can just push all new attributes into the dynamic component.
        if(iter1 == oldAttributes.end())
        {
            for(;iter2 != deserializedAttributes.end(); ++iter2)
                addAttributes.push_back(*iter2);
            break;
        }
        // Only old attributes are left and they can be removed from the dynamic component.
        else if(iter2 == deserializedAttributes.end())
        {
            for(;iter1 != oldAttributes.end(); ++iter1)
                remAttributeIds.push_back((*iter1)->Id());
            break;
        }

        // Attribute has already created and we only need to update it's value.
        if((*iter1)->Id().compare((*iter2).id, Qt::CaseInsensitive) == 0)
        {
            //SetAttribute(QString::fromStdString(iter2->name_), QString::fromStdString(iter2->value_), change);
            for(AttributeVector::const_iterator attr_iter = attributes.begin(); attr_iter != attributes.end(); ++attr_iter)
                if((*attr_iter)->Id().compare(iter2->id, Qt::CaseInsensitive) == 0)
                {
                    (*attr_iter)->FromString(iter2->value, change);
                    break;
                }

            ++iter2;
            ++iter1;
        }
        // Found a new attribute that need to be created and added to the component.
        else if((*iter1)->Id() > (*iter2).id)
        {
            addAttributes.push_back(*iter2);
            ++iter2;
        }
        // Couldn't find the attribute in a new list so it need to be removed from the component.
        else
        {
            remAttributeIds.push_back((*iter1)->Id());
            ++iter1;
        }
    }

    foreach(const AttributeDesc &a, addAttributes)
    {
        IAttribute *attribute = CreateAttribute(a.typeName, a.id);
        if (attribute)
            attribute->FromString(a.value, change);
    }

    foreach(const QString &id, remAttributeIds)
        RemoveAttribute(id);
}

IAttribute *EC_DynamicComponent::CreateAttribute(const QString &typeName, const QString &id, AttributeChange::Type change)
{
    if (ContainsAttribute(id))
        return IComponent::AttributeById(id);

    IAttribute *attribute = SceneAPI::CreateAttribute(typeName, id);
    if (!attribute)
    {
        LogError("Failed to create new attribute of type \"" + typeName + "\" with ID \"" + id + "\" to dynamic component \"" + Name() + "\".");
        return 0;
    }

    IComponent::AddAttribute(attribute);

    Scene* scene = ParentScene();
    if (scene)
        scene->EmitAttributeAdded(this, attribute, change);

    emit AttributeAdded(attribute);
    EmitAttributeChanged(attribute, change);
    return attribute;
}

void EC_DynamicComponent::RemoveAttribute(const QString &id, AttributeChange::Type change)
{
    for(AttributeVector::const_iterator iter = attributes.begin(); iter != attributes.end(); ++iter)
        if(*iter && (*iter)->Id().compare(id, Qt::CaseInsensitive) == 0)
        {
            IComponent::RemoveAttribute((*iter)->Index(), change);
            break;
        }
}

void EC_DynamicComponent::RemoveAllAttributes(AttributeChange::Type change)
{
    for(AttributeVector::const_iterator iter = attributes.begin(); iter != attributes.end(); ++iter)
        if (*iter)
            IComponent::RemoveAttribute((*iter)->Index(), change);

    attributes.clear();
}

int EC_DynamicComponent::GetInternalAttributeIndex(int index) const
{
    if (index < 0 || index >= (int)attributes.size())
        return -1; // Can be sure that is not found.
    int cmp = 0;
    for (size_t i = 0; i < attributes.size(); ++i)
    {
        if (!attributes[i])
            continue;
        if (cmp == index)
            return i;
        ++cmp;
    }
    return -1;
}

void EC_DynamicComponent::AddQVariantAttribute(const QString &id, AttributeChange::Type change)
{
    LogWarning("EC_DynamicComponent::AddQVariantAttribute is deprecated and will be removed. Use CreateAttribute(\"QVariant\",...) instead.");
    //Check if the attribute has already been created.
    if(!ContainsAttribute(id))
    {
        ::Attribute<QVariant> *attribute = new ::Attribute<QVariant>(this, id.toStdString().c_str());
        EmitAttributeChanged(attribute, change);
        emit AttributeAdded(attribute);
    }
    else
        LogWarning("Failed to add a new QVariant with ID " + id + ", because there already is an attribute with that ID.");
}

QVariant EC_DynamicComponent::Attribute(int index) const
{
    // Do not count holes.
    int attrIndex = GetInternalAttributeIndex(index);
    if (attrIndex < 0)
        return QVariant();
    return attributes[attrIndex]->ToQVariant();
}

QVariant EC_DynamicComponent::Attribute(const QString &id) const
{
    return IComponent::GetAttributeQVariant(id);
}

void EC_DynamicComponent::SetAttribute(int index, const QVariant &value, AttributeChange::Type change)
{
    int attrIndex = GetInternalAttributeIndex(index);
    if (attrIndex < 0)
    {
        LogWarning("Cannot set attribute, index out of bounds");
        return;
    }
    
    attributes[attrIndex]->FromQVariant(value, change);
}

void EC_DynamicComponent::SetAttribute(const QString &id, const QVariant &value, AttributeChange::Type change)
{
    IAttribute* attr = AttributeById(id);
    if (attr)
        attr->FromQVariant(value, change);
}

QString EC_DynamicComponent::GetAttributeName(int index) const
{
    LogWarning("EC_DynamicComponent::GetAttributeName is deprecated and will be removed. For dynamic attributes ID is the same as name. Use GetAttributeId instead.");
    
    // Do not count holes.
    int attrIndex = GetInternalAttributeIndex(index);
    if (attrIndex < 0)
    {
        LogWarning("Cannot get attribute name, index out of bounds");
        return QString();
    }
    return attributes[index]->Name();
}

QString EC_DynamicComponent::GetAttributeId(int index) const
{
    // Do not count holes.
    int attrIndex = GetInternalAttributeIndex(index);
    if (attrIndex < 0)
    {
        LogWarning("Cannot get attribute ID, index out of bounds");
        return QString();
    }
    return attributes[index]->Id();
}

bool EC_DynamicComponent::ContainSameAttributes(const EC_DynamicComponent &comp) const
{
    AttributeVector myAttributeVector = NonEmptyAttributes();
    AttributeVector attributeVector = comp.NonEmptyAttributes();
    if(attributeVector.size() != myAttributeVector.size())
        return false;
    if(attributeVector.empty() && myAttributeVector.empty())
        return true;

    std::sort(myAttributeVector.begin(), myAttributeVector.end(), &CmpAttributeById);
    std::sort(attributeVector.begin(), attributeVector.end(), &CmpAttributeById);

    AttributeVector::const_iterator iter1 = myAttributeVector.begin();
    AttributeVector::const_iterator iter2 = attributeVector.begin();
    while(iter1 != myAttributeVector.end() && iter2 != attributeVector.end())
    {
        // Compare attribute names and type and if they mach continue iteration if not components aren't exactly the same.
        if (((*iter1)->Id().compare((*iter2)->Id(), Qt::CaseInsensitive) == 0) &&
            (*iter1)->TypeName().compare((*iter2)->TypeName(), Qt::CaseInsensitive) == 0)
        {
            if(iter1 != myAttributeVector.end())
                ++iter1;
            if(iter2 != attributeVector.end())
                ++iter2;
        }
        else
        {
            return false;
        }
    }
    return true;

    /*// Get both attributes and check if they are holding exact number of attributes.
    AttributeVector myAttributeVector = Attributes();
    AttributeVector attributeVector = comp.Attributes();
    if(attributeVector.size() != myAttributeVector.size())
        return false;
    
    // Compare that every attribute is same in both components.
    QSet<IAttribute*> myAttributeSet;
    QSet<IAttribute*> attributeSet;
    for(uint i = 0; i < myAttributeSet.size(); i++)
    {
        attributeSet.insert(myAttributeVector[i]);
        myAttributeSet.insert(attributeVector[i]);
    }
    if(attributeSet != myAttributeSet)
        return false;
    return true;*/
}

bool EC_DynamicComponent::ContainsAttribute(const QString &id) const
{
    return AttributeById(id) != 0;
}

void EC_DynamicComponent::SerializeToBinary(kNet::DataSerializer& dest) const
{
    dest.Add<u8>((u8)attributes.size());
    // For now, transmit all values as strings
    AttributeVector::const_iterator iter = attributes.begin();
    while(iter != attributes.end())
    {
        if (*iter)
        {
            dest.AddString((*iter)->Id().toStdString());
            dest.AddString((*iter)->TypeName().toStdString());
            dest.AddString((*iter)->ToString().toStdString()); /**< @todo Use UTF-8, see Attribute<QString>::ToBinary */
        }
        ++iter;
    }
}

void EC_DynamicComponent::DeserializeFromBinary(kNet::DataDeserializer& source, AttributeChange::Type change)
{
    const u8 num_attributes = source.Read<u8>();
    std::vector<AttributeDesc> deserializedAttributes;
    for(uint i = 0; i < num_attributes; ++i)
    {
        AttributeDesc attrData;
        attrData.id = QString::fromStdString(source.ReadString());
        attrData.typeName = QString::fromStdString(source.ReadString());
        attrData.value = QString::fromStdString(source.ReadString());

        deserializedAttributes.push_back(attrData);
    }

    DeserializeCommon(deserializedAttributes, change);
}
