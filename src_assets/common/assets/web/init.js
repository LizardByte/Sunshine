import i18n from './locale'

export function initApp(app, config) {
    //Wait for locale initialization, then render
    i18n().then(i18n => {
        app.use(i18n);
        app.provide('i18n', i18n.global)
        app.mount('#app');
        if (config) {
            config(app)
        }
    });
}
