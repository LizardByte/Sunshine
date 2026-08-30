/**
 * Keep matching groups of tabs in sync.
 */
(function() {
    'use strict';

    let synchronizing = false;

    function getTabButtons(group) {
        const tabList = Array.from(group.children).find((child) => child.getAttribute('role') === 'tablist');

        if (!tabList) {
            return [];
        }

        return Array.from(tabList.querySelectorAll('[role="tab"][data-tab-name]'));
    }

    function haveMatchingNames(firstButtons, secondButtons) {
        return firstButtons.length === secondButtons.length && firstButtons.every(
            (button, index) => button.dataset.tabName === secondButtons[index].dataset.tabName
        );
    }

    function synchronizeTabs(event) {
        const selectedTab = event.target;

        if (synchronizing || !selectedTab.matches('.tabs [role="tab"][data-tab-name]')) {
            return;
        }

        const selectedGroup = selectedTab.closest('.tabs');
        const selectedButtons = getTabButtons(selectedGroup);
        const selectedIndex = selectedButtons.indexOf(selectedTab);

        synchronizing = true;

        try {
            document.querySelectorAll('.tabs').forEach((group) => {
                const groupButtons = getTabButtons(group);
                const matchingTab = groupButtons[selectedIndex];

                if (
                    group !== selectedGroup &&
                    haveMatchingNames(selectedButtons, groupButtons) &&
                    !matchingTab.classList.contains('active')
                ) {
                    bootstrap.Tab.getOrCreateInstance(matchingTab).show();
                }
            });
        } finally {
            synchronizing = false;
        }
    }

    document.addEventListener('shown.bs.tab', synchronizeTabs);
})();
