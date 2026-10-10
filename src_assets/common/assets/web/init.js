import i18n from './locale'

import 'bootstrap/dist/css/bootstrap.min.css'
// Load Sunshine.css after bootstrap to override some of the styles.
// Makes themes load and style correctly.
import './sunshine.css'

// must import even if not implicitly using here
// https://github.com/aurelia/skeleton-navigation/issues/894
// https://discourse.aurelia.io/t/bootstrap-import-bootstrap-breaks-dropdown-menu-in-navbar/641/9
import 'bootstrap/dist/js/bootstrap'

/**
 * @brief Mount the application after locale initialization and report startup failures.
 *
 * @param {import('vue').App} app Application to configure and mount.
 * @param {Function} [config] Optional callback invoked after mounting.
 */
export function initApp(app, config) {
    //Wait for locale initialization, then render
    i18n().then(i18n => {
        app.use(i18n);
        app.provide('i18n', i18n.global)
        app.mount('#app');
        if (config) {
            config(app)
        }
    }).catch(error => {
        console.error('Failed to initialize Sunshine', error);
    });
}
