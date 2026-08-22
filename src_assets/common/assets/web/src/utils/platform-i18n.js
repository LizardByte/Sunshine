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
     * @param {string} platform identifier
     * @return {string} key with platform identifier
     */
    getPlatformKey(key, platform) {
        return key + '_' + platform
    }

    /**
     * @param {string} key
     * @param {string?} defaultMsg
     * @return {string} translated message or defaultMsg if provided
     */
    getMessageUsingPlatform(key, defaultMsg) {
        const realKey = this.getPlatformKey(key, this.platform)
        const i18n = inject('i18n')
        let message = i18n.t(realKey)

        if (message !== realKey) {
            // We got a message back, return early
            return message
        }
        
        // If on Windows, we don't fallback to unix, so return early
        if (this.platform === 'windows') {
            return defaultMsg ? defaultMsg : message
        }
        
        // there's no message for key, check for unix version
        const unixKey = this.getPlatformKey(key, 'unix')
        message = i18n.t(unixKey)

        if (message === unixKey && defaultMsg) {
            // there's no message for unix key, return defaultMsg
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
