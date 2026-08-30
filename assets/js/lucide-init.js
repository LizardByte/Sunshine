/**
 * Replace Lucide icon placeholders with inline SVGs.
 */
(function() {
    'use strict';

    if (!globalThis.lucide) {
        return;
    }

    globalThis.lucide.createIcons({ icons: globalThis.lucide.icons });
})();
