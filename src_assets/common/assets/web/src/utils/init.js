import i18n from './locale'
import router from './router'
// must import even if not implicitly using here
// https://github.com/aurelia/skeleton-navigation/issues/894
// https://discourse.aurelia.io/t/bootstrap-import-bootstrap-breaks-dropdown-menu-in-navbar/641/9
import 'bootstrap/dist/js/bootstrap'

export function initApp(app, config) {
    //Wait for locale initialization, then render
    i18n().then(i18n => {
        app.use(i18n);
        app.use(router);
        app.provide('i18n', i18n.global)
        app.directive('dropdown-show', {
            mounted: function (el, binding) {
                el.addEventListener('show.bs.dropdown', binding.value);
            }
        });
        app.mount('#app');
        if (config) {
            config(app)
        }
    });
}
