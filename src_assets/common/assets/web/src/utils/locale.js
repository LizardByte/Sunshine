import {createI18n} from "vue-i18n";

export const SUPPORTED_LOCALES = [
    {value: 'bg', label: 'Български (Bulgarian)'},
    {value: 'cs', label: 'Čeština (Czech)'},
    {value: 'de', label: 'Deutsch (German)'},
    {value: 'en', label: 'English'},
    {value: 'en_GB', label: 'English, United Kingdom'},
    {value: 'en_US', label: 'English, United States'},
    {value: 'es', label: 'Español (Spanish)'},
    {value: 'fr', label: 'Français (French)'},
    {value: 'hu', label: 'Magyar (Hungarian)'},
    {value: 'it', label: 'Italiano (Italian)'},
    {value: 'ja', label: '日本語 (Japanese)'},
    {value: 'ko', label: '한국어 (Korean)'},
    {value: 'pl', label: 'Polski (Polish)'},
    {value: 'pt', label: 'Português (Portuguese)'},
    {value: 'pt_BR', label: 'Português, Brasileiro (Portuguese, Brazilian)'},
    {value: 'ru', label: 'Русский (Russian)'},
    {value: 'sv', label: 'svenska (Swedish)'},
    {value: 'tr', label: 'Türkçe (Turkish)'},
    {value: 'uk', label: 'Українська (Ukranian)'},
    {value: 'vi', label: 'Tiếng Việt (Vietnamese)'},
    {value: 'zh', label: '简体中文 (Chinese Simplified)'},
    {value: 'zh_TW', label: '繁體中文 (Chinese Traditional)'},
]

export default async function() {
    let r = await (await fetch("./api/configLocale")).json();
    let locale = r.locale ?? "en";
    document.querySelector('html').setAttribute('lang', locale);
    // Fetch only the fallback language files
    let en = await (await fetch('./assets/locale/en.json')).json();
    let messages = {
        en
    };
    try {
        if (locale !== 'en') {
            let r = await (await fetch(`./assets/locale/${locale}.json`)).json();
            messages[locale] = r;
        }
    } catch (e) {
        console.error("Failed to download translations", e);
    }
    const i18n = createI18n({
        locale: locale, // set locale
        fallbackLocale: 'en', // set fallback locale
        messages: messages
    })
    return i18n;
}
