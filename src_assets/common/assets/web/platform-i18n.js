import {inject} from 'vue'

class PlatformMessageI18n {
    /**
     * @param {string} platform
     */
    constructor(platform) {
        this.platform = platform
    }

    /**
     * @param {string} key
     * @return {string} key with platform identifier
     */
    getPlatformKey(key) {
        switch (this.platform) {
            case 'windows':
                return key + '_win'
            default:
                return key + '_' + this.platform
        }
    }

    /**
     * @param {string} key
     * @param {string?} defaultMsg
     * @return {string} translated message or defaultMsg if provided
     */
    getMessageUsingPlatform(key, defaultMsg) {
        const realKey = this.getPlatformKey(key)
        const i18n = inject('i18n')
        const message = i18n.t(realKey)
        if (message === realKey && defaultMsg) {
            // there's no message for key, return defaultMsg
            return defaultMsg
        }
        return message
    }
}

/**
 * @param {string?} platform
 * @return {PlatformMessageI18n} instance
 */
export function usePlatformI18n(platform) {
    if (!platform) {
        platform = inject('platform').value
    }

    if (!platform) {
        throw 'platform argument missing'
    }

    return inject(
        'platformMessage',
        () => new PlatformMessageI18n(platform),
        true
    )
}

/**
 * @param {string} key
 * @param {string?} defaultMsg
 * @return {string} translated message or defaultMsg if provided
 */
export function $tp(key, defaultMsg) {
    const pm = usePlatformI18n()
    return pm.getMessageUsingPlatform(key, defaultMsg)
}
