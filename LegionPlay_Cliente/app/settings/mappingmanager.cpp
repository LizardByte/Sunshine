#include "mappingmanager.h"
#include "path.h"

#include <QDir>

#include "SDL_compat.h"

#define SER_GAMEPADMAPPING "gcmapping"

#define SER_GUID "guid"
#define SER_MAPPING "mapping"

MappingFetcher* MappingManager::s_MappingFetcher;

MappingManager::MappingManager()
{
    QSettings settings;

    // Load updated mappings from the Internet once per Moonlight launch
    if (s_MappingFetcher == nullptr) {
        s_MappingFetcher = new MappingFetcher();
        s_MappingFetcher->start();
    }

    // First load existing saved mappings. This ensures the user's
    // hints can always override the old data.
    int mappingCount = settings.beginReadArray(SER_GAMEPADMAPPING);
    for (int i = 0; i < mappingCount; i++) {
        settings.setArrayIndex(i);

        SdlGamepadMapping mapping(settings.value(SER_GUID).toString(), settings.value(SER_MAPPING).toString());
        addMapping(mapping);
    }
    settings.endArray();

    // Finally load mappings from SDL_HINT_GAMECONTROLLERCONFIG
    QStringList sdlMappings =
            QString::fromLocal8Bit(SDL_GetHint(SDL_HINT_GAMECONTROLLERCONFIG))
        #if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
            .split('\n', Qt::SkipEmptyParts);
        #else
            .split('\n', QString::SkipEmptyParts);
        #endif
    for (const QString& sdlMapping : sdlMappings) {
        SdlGamepadMapping mapping(sdlMapping);
        addMapping(mapping);
    }

    // Save the updated mappings to settings
    save();
}

void MappingManager::save()
{
    QSettings settings;

    settings.remove(SER_GAMEPADMAPPING);
    settings.beginWriteArray(SER_GAMEPADMAPPING);
    QList<SdlGamepadMapping> mappings = m_Mappings.values();
    for (int i = 0; i < mappings.count(); i++) {
        settings.setArrayIndex(i);

        settings.setValue(SER_GUID, mappings[i].getGuid());
        settings.setValue(SER_MAPPING, mappings[i].getMapping());
    }
    settings.endArray();
}

void MappingManager::applyMappings()
{
    QByteArray mappingData = Path::readDataFile("gamecontrollerdb.txt");
    if (!mappingData.isEmpty()) {
        int newMappings = SDL_GameControllerAddMappingsFromRW(
                    SDL_RWFromConstMem(mappingData.constData(), mappingData.size()), 1);

        if (newMappings > 0) {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "Loaded %d new gamepad mappings",
                        newMappings);
        }
        else {
            if (newMappings < 0) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                             "Error loading gamepad mappings: %s",
                             SDL_GetError());
            }
            else if (newMappings == 0) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "0 new mappings found in gamecontrollerdb.txt. Is it corrupt?");
            }

            // Try deleting the cached mapping list just in case it's corrupt
            Path::deleteCacheFile("gamecontrollerdb.txt");
        }
    }
    else {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Unable to load gamepad mapping file");
    }

    QList<SdlGamepadMapping> mappings = m_Mappings.values();
    for (const SdlGamepadMapping& mapping : mappings) {
        QString sdlMappingString = mapping.getSdlMappingString();
        int ret = SDL_GameControllerAddMapping(qPrintable(sdlMappingString));
        if (ret < 0) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "Unable to add mapping: %s",
                        qPrintable(sdlMappingString));
        }
        else if (ret == 1) {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "Loaded saved user mapping: %s",
                        qPrintable(sdlMappingString));
        }
    }
}

void MappingManager::addMapping(QString mappingString)
{
    SdlGamepadMapping mapping(mappingString);
    addMapping(mapping);
}

void MappingManager::addMapping(SdlGamepadMapping& mapping)
{
    m_Mappings[mapping.getGuid()] = mapping;
}
