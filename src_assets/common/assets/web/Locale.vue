<template>
</template>

<script>
import { createI18n } from "vue-i18n";

// Import translation files
import de from './locale/de.json'
import en from './locale/en.json'
import en_GB from './locale/en-GB.json'
import en_US from './locale/en-US.json'
import es from './locale/es.json'
import fr from './locale/fr.json'
import it from './locale/it.json'
import ru from './locale/ru.json'
import sv from './locale/sv.json'
import zh from './locale/zh.json'

// Create the i18n instance
const i18n = createI18n({
    locale: "en",  // initial locale, todo: how to get this from config?
    fallbackLocale: "en",  // fallback locale
    messages: {
        de: de,
        en: en,
        "en-GB": en_GB,
        "en-US": en_US,
        es: es,
        fr: fr,
        it: it,
        ru: ru,
        sv: sv,
        zh: zh
    },
});

export {
    i18n
};

export default {
    created() {
        this.fetchLocale();
    },
    methods: {
        fetchLocale() {
            fetch("/api/configLocale$")
                .then((r) => r.json())
                .then((r) => {
                    this.response = r;
                    i18n.locale = this.response.locale;
                });
        },
    },
};
</script>
