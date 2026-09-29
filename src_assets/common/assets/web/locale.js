import {createI18n} from "vue-i18n";

// Import only the fallback language files
import en from './public/assets/locale/en.json'

const localePaths = new Map([
    ['bg', './assets/locale/bg.json'],
    ['cs', './assets/locale/cs.json'],
    ['de', './assets/locale/de.json'],
    ['en_GB', './assets/locale/en_GB.json'],
    ['en_US', './assets/locale/en_US.json'],
    ['es', './assets/locale/es.json'],
    ['fr', './assets/locale/fr.json'],
    ['hu', './assets/locale/hu.json'],
    ['it', './assets/locale/it.json'],
    ['ja', './assets/locale/ja.json'],
    ['ko', './assets/locale/ko.json'],
    ['pl', './assets/locale/pl.json'],
    ['pt', './assets/locale/pt.json'],
    ['pt_BR', './assets/locale/pt_BR.json'],
    ['ru', './assets/locale/ru.json'],
    ['sv', './assets/locale/sv.json'],
    ['tr', './assets/locale/tr.json'],
    ['uk', './assets/locale/uk.json'],
    ['vi', './assets/locale/vi.json'],
    ['zh', './assets/locale/zh.json'],
    ['zh_TW', './assets/locale/zh_TW.json'],
]);

/**
 * @brief Resolve a configured locale to a bundled translation file.
 *
 * @param {unknown} configuredLocale Locale returned by the configuration API.
 * @return {string} Supported locale identifier or the English fallback.
 */
function resolveLocale(configuredLocale) {
    if (typeof configuredLocale !== 'string') {
        return 'en';
    }

    return configuredLocale === 'en' || localePaths.has(configuredLocale) ? configuredLocale : 'en';
}

/**
 * @brief Load messages for a supported locale with an English fallback.
 *
 * @param {string} locale Supported locale identifier.
 * @param {Function} loadTranslation Function that loads a translation path.
 * @return {Promise<{locale: string, messages: object}>} Loaded locale and messages.
 */
export async function loadLocaleMessages(
    locale,
    loadTranslation = async path => (await fetch(path)).json(),
) {
    let messages = { en };

    try {
        if (locale !== 'en') {
            const translation = await loadTranslation(localePaths.get(locale));
            messages = Object.fromEntries([
                ['en', en],
                [locale, translation],
            ]);
        }
    } catch (e) {
        console.error("Failed to download translations", e);
        locale = 'en';
    }

    return { locale, messages };
}

/**
 * @brief Create the Vue internationalization instance for the configured locale.
 *
 * @return {Promise<import('vue-i18n').I18n>} Configured internationalization instance.
 */
export default async function createSunshineI18n() {
    const localeConfig = await (await fetch("./api/configLocale")).json();
    const configuredLocale = resolveLocale(localeConfig.locale);
    const { locale, messages } = await loadLocaleMessages(configuredLocale);

    document.documentElement.setAttribute('lang', locale);
    const i18n = createI18n({
        locale: locale, // set locale
        fallbackLocale: 'en', // set fallback locale
        messages: messages
    })
    return i18n;
}
