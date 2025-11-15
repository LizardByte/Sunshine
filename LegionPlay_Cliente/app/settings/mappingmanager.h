#pragma once

#include "mappingfetcher.h"

#include <QSettings>

class SdlGamepadMapping
{
public:
    SdlGamepadMapping() {}

    SdlGamepadMapping(QString string)
    {
        QStringList mapping = string.split(",");
        if (!mapping.isEmpty()) {
            m_Guid = mapping[0];

            string.remove(0, m_Guid.length() + 1);
            m_Mapping = string;
        }
    }

    SdlGamepadMapping(QString guid, QString mapping)
        : m_Guid(guid),
          m_Mapping(mapping)
    {

    }

    bool operator==(const SdlGamepadMapping& other) const
    {
        return m_Guid == other.m_Guid && m_Mapping == other.m_Mapping;
    }

    QString getGuid() const
    {
        return m_Guid;
    }

    QString getMapping() const
    {
        return m_Mapping;
    }

    QString getSdlMappingString() const
    {
        if (m_Guid.isEmpty() || m_Mapping.isEmpty()) {
            return "";
        }
        else {
            return m_Guid + "," + m_Mapping;
        }
    }

private:
    QString m_Guid;
    QString m_Mapping;
};

class MappingManager
{
public:
    MappingManager();

    void addMapping(QString gamepadString);

    void addMapping(SdlGamepadMapping& gamepadMapping);

    void applyMappings();

    void save();

private:
    QMap<QString, SdlGamepadMapping> m_Mappings;

    static MappingFetcher* s_MappingFetcher;
};

