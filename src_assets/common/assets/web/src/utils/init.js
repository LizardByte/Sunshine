import i18n from '@/utils/locale'

import 'bootstrap/dist/css/bootstrap.min.css'
// Load Sunshine.css after bootstrap to override some of the styles.
// Makes themes load and style correctly.
import '../assets/sunshine.css'

import 'bootstrap'

export function initApp(app, config) {
    //Wait for locale initialization, then render
    i18n().then(i18n => {
        app.use(i18n);
        app.provide('i18n', i18n.global)
        if (config) {
            config(app)
        }
        app.mount('#app');
    });
}
