import {createI18n} from "vue-i18n";

// Import only the fallback language files
import en from './public/assets/locale/en.json'

export default async function() {
    let r = await (await fetch("/api/configLocale")).json();
    let locale = r.locale ?? "en";
    document.querySelector('html').setAttribute('lang', locale);
    let messages = {
        en
    };
    try {
        if (locale !== 'en') {
            let r = await (await fetch(`/assets/locale/${locale}.json`)).json();
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
