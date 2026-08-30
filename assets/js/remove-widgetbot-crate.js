function removeWidgetbotCrates() {
    document.querySelectorAll("widgetbot-crate").forEach(function (crate) {
        crate.remove()
    })
}

if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", removeWidgetbotCrates)
} else {
    removeWidgetbotCrates()
}
