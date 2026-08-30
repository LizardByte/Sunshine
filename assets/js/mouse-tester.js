function formatSigned(value) {
    const roundedValue = Math.round(value);
    return roundedValue > 0 ? `+${roundedValue}` : `${roundedValue}`;
}

function wheelUnit(deltaMode) {
    if (deltaMode === WheelEvent.DOM_DELTA_LINE) return 'lines';
    if (deltaMode === WheelEvent.DOM_DELTA_PAGE) return 'pages';
    return 'px';
}

function wheelModeName(deltaMode) {
    if (deltaMode === WheelEvent.DOM_DELTA_LINE) return 'Lines';
    if (deltaMode === WheelEvent.DOM_DELTA_PAGE) return 'Pages';
    return 'Pixels';
}

function wheelDirection(deltaX, deltaY) {
    if (Math.abs(deltaY) >= Math.abs(deltaX) && deltaY !== 0) {
        return deltaY < 0 ? '↑ Up' : '↓ Down';
    }
    if (deltaX !== 0) {
        return deltaX < 0 ? '← Left' : '→ Right';
    }
    return 'Idle';
}

function preventBrowserAction(event) {
    event.preventDefault();
}

document.addEventListener('DOMContentLoaded', function() {
    const buttonConfigs = [
        { number: 0, bit: 1, name: 'primary' },
        { number: 1, bit: 4, name: 'wheel' },
        { number: 2, bit: 2, name: 'secondary' },
        { number: 3, bit: 8, name: 'back' },
        { number: 4, bit: 16, name: 'forward' }
    ];
    const browserNavigationButtons = new Set([3, 4]);
    const buttonPressCounts = new Map(buttonConfigs.map(config => [config.number, 0]));
    const pressedButtons = new Set();
    const suppressedNavigationButtons = new Set();
    const captureSurface = document.getElementById('mouse-capture-surface');
    const positionIndicator = document.getElementById('mouse-position-indicator');
    const status = document.getElementById('mouse-status');
    const statusMessage = document.getElementById('mouse-status-message');
    const activeButtonsOutput = document.getElementById('mouse-active-buttons');
    const lastEventOutput = document.getElementById('mouse-last-event');
    const wheelModeOutput = document.getElementById('mouse-wheel-mode');
    const browserSupportOutput = document.getElementById('mouse-browser-support');
    const firefoxMouseButtonsWarning = document.getElementById('firefox-mouse-buttons-warning');
    const movementXOutput = document.getElementById('mouse-movement-x');
    const movementYOutput = document.getElementById('mouse-movement-y');
    const positionOutput = document.getElementById('mouse-position');
    const distanceOutput = document.getElementById('mouse-distance');
    const moveEventsOutput = document.getElementById('mouse-move-events');
    const doubleClicksOutput = document.getElementById('mouse-double-clicks');
    const wheelXOutput = document.getElementById('mouse-wheel-x');
    const wheelYOutput = document.getElementById('mouse-wheel-y');
    const wheelDirectionOutput = document.getElementById('mouse-wheel-direction');
    const wheelEventsOutput = document.getElementById('mouse-wheel-events');
    let hasReceivedInput = false;
    let scrollArrowTimer = null;
    let motionFrameId = null;
    let movementX = 0;
    let movementY = 0;
    let positionX = 0;
    let positionY = 0;
    let totalDistance = 0;
    let moveEvents = 0;
    let doubleClicks = 0;
    let wheelX = 0;
    let wheelY = 0;
    let wheelEvents = 0;
    let wheelDeltaMode = 0;

    firefoxMouseButtonsWarning.hidden = !navigator.userAgent.includes('Firefox/');

    function updateStatus(message, type) {
        status.classList.remove('alert-warning', 'alert-success', 'alert-danger');
        status.classList.add(`alert-${type}`);
        statusMessage.textContent = message;
    }

    function markInput(eventName) {
        lastEventOutput.textContent = eventName;
        if (!hasReceivedInput) {
            hasReceivedInput = true;
            updateStatus('Mouse input detected. Keep testing buttons, movement, and scrolling.', 'success');
        }
    }

    function getButtonElements(buttonNumber) {
        return {
            card: document.querySelector(`.mouse-button-card[data-mouse-button="${buttonNumber}"]`),
            count: document.getElementById(`mouse-button-${buttonNumber}-count`),
            state: document.getElementById(`mouse-button-${buttonNumber}-state`),
            visual: document.querySelector(`.mouse-svg-control[data-mouse-button="${buttonNumber}"]`)
        };
    }

    function renderButton(buttonNumber) {
        const elements = getButtonElements(buttonNumber);
        const isActive = pressedButtons.has(buttonNumber);
        elements.card.classList.toggle('is-active', isActive);
        elements.visual.classList.toggle('is-active', isActive);
        elements.state.textContent = isActive ? 'Pressed' : 'Released';
        elements.count.textContent = buttonPressCounts.get(buttonNumber);
    }

    function renderAllButtons() {
        buttonConfigs.forEach(config => renderButton(config.number));
    }

    function renderActiveButtons(buttonsBitmask) {
        const activeNames = buttonConfigs
            .filter(config => (buttonsBitmask & config.bit) !== 0)
            .map(config => config.name);
        activeButtonsOutput.textContent = activeNames.length === 0
            ? '0 (none)'
            : `${buttonsBitmask} (${activeNames.join(' + ')})`;
    }

    function syncPressedButtons(buttonsBitmask) {
        pressedButtons.clear();
        buttonConfigs.forEach(config => {
            if ((buttonsBitmask & config.bit) !== 0) {
                pressedButtons.add(config.number);
            }
        });
        renderActiveButtons(buttonsBitmask);
        renderAllButtons();
    }

    function renderMotion() {
        movementXOutput.textContent = formatSigned(movementX);
        movementYOutput.textContent = formatSigned(movementY);
        positionOutput.textContent = `${Math.round(positionX)}, ${Math.round(positionY)}`;
        distanceOutput.textContent = Math.round(totalDistance);
        moveEventsOutput.textContent = moveEvents;
        positionIndicator.style.left = `${positionX}px`;
        positionIndicator.style.top = `${positionY}px`;
        positionIndicator.hidden = false;
        motionFrameId = null;
    }

    function scheduleMotionRender() {
        if (motionFrameId === null) {
            motionFrameId = requestAnimationFrame(renderMotion);
        }
    }

    function showScrollArrows(deltaX, deltaY) {
        if (scrollArrowTimer !== null) {
            clearTimeout(scrollArrowTimer);
        }
        const activeDirections = new Set();
        if (deltaX < 0) activeDirections.add('left');
        if (deltaX > 0) activeDirections.add('right');
        if (deltaY < 0) activeDirections.add('up');
        if (deltaY > 0) activeDirections.add('down');
        document.querySelectorAll('.mouse-svg-scroll-indicator').forEach(indicator => {
            indicator.classList.toggle('is-active', activeDirections.has(indicator.dataset.scrollDirection));
        });
        scrollArrowTimer = setTimeout(function() {
            document.querySelectorAll('.mouse-svg-scroll-indicator').forEach(indicator => {
                indicator.classList.remove('is-active');
            });
            scrollArrowTimer = null;
        }, 450);
    }

    function recordButtonDown(event) {
        const wasPressed = pressedButtons.has(event.button);
        captureSurface.focus({ preventScroll: true });
        markInput(`Button ${event.button} down`);
        syncPressedButtons(event.buttons);
        if (!wasPressed && buttonPressCounts.has(event.button)) {
            buttonPressCounts.set(event.button, buttonPressCounts.get(event.button) + 1);
            renderButton(event.button);
        }
    }

    function handleMouseDown(event) {
        event.preventDefault();
        recordButtonDown(event);
    }

    function handleSideButtonPointerDown(event) {
        if (!browserNavigationButtons.has(event.button)) return;
        event.preventDefault();
        suppressedNavigationButtons.add(event.button);
        recordButtonDown(event);
    }

    function handleSideButtonPointerUp(event) {
        if (!browserNavigationButtons.has(event.button) || !suppressedNavigationButtons.has(event.button)) return;
        event.preventDefault();
        syncPressedButtons(event.buttons);
        markInput(`Button ${event.button} up`);
        suppressedNavigationButtons.delete(event.button);
    }

    function handleMouseMove(event) {
        const surfaceBounds = captureSurface.getBoundingClientRect();
        movementX = event.movementX;
        movementY = event.movementY;
        positionX = Math.min(Math.max(event.clientX - surfaceBounds.left, 0), surfaceBounds.width);
        positionY = Math.min(Math.max(event.clientY - surfaceBounds.top, 0), surfaceBounds.height);
        totalDistance += Math.hypot(movementX, movementY);
        moveEvents += 1;
        markInput('Mouse move');
        syncPressedButtons(event.buttons);
        scheduleMotionRender();
    }

    function handleWheel(event) {
        event.preventDefault();
        wheelDeltaMode = event.deltaMode;
        wheelX += event.deltaX;
        wheelY += event.deltaY;
        wheelEvents += 1;
        const unit = wheelUnit(wheelDeltaMode);
        wheelXOutput.textContent = `${formatSigned(wheelX)} ${unit}`;
        wheelYOutput.textContent = `${formatSigned(wheelY)} ${unit}`;
        wheelDirectionOutput.textContent = wheelDirection(event.deltaX, event.deltaY);
        wheelEventsOutput.textContent = wheelEvents;
        wheelModeOutput.textContent = wheelModeName(wheelDeltaMode);
        markInput('Mouse wheel');
        showScrollArrows(event.deltaX, event.deltaY);
    }

    function releaseButtons(event) {
        syncPressedButtons(event?.buttons ?? 0);
        if (event && captureSurface.contains(event.target)) {
            event.preventDefault();
            markInput(`Button ${event.button} up`);
        }
    }

    function preventSideButtonBrowserAction(event) {
        if (browserNavigationButtons.has(event.button)) {
            event.preventDefault();
        }
    }

    function releaseAllButtons() {
        suppressedNavigationButtons.clear();
        syncPressedButtons(0);
    }

    function resetTester() {
        hasReceivedInput = false;
        buttonConfigs.forEach(config => buttonPressCounts.set(config.number, 0));
        pressedButtons.clear();
        suppressedNavigationButtons.clear();
        if (scrollArrowTimer !== null) {
            clearTimeout(scrollArrowTimer);
            scrollArrowTimer = null;
        }
        document.querySelectorAll('.mouse-svg-scroll-indicator').forEach(indicator => {
            indicator.classList.remove('is-active');
        });
        if (motionFrameId !== null) {
            cancelAnimationFrame(motionFrameId);
            motionFrameId = null;
        }
        movementX = 0;
        movementY = 0;
        positionX = 0;
        positionY = 0;
        totalDistance = 0;
        moveEvents = 0;
        doubleClicks = 0;
        wheelX = 0;
        wheelY = 0;
        wheelEvents = 0;
        wheelDeltaMode = 0;
        movementXOutput.textContent = '0';
        movementYOutput.textContent = '0';
        positionOutput.textContent = '0, 0';
        distanceOutput.textContent = '0';
        moveEventsOutput.textContent = '0';
        doubleClicksOutput.textContent = '0';
        wheelXOutput.textContent = '0 px';
        wheelYOutput.textContent = '0 px';
        wheelDirectionOutput.textContent = 'Idle';
        wheelEventsOutput.textContent = '0';
        wheelModeOutput.textContent = 'Pixels';
        lastEventOutput.textContent = 'None';
        positionIndicator.hidden = true;
        renderActiveButtons(0);
        renderAllButtons();
        updateStatus('Move, click, or scroll inside the test area to begin.', 'warning');
    }

    if (globalThis.MouseEvent === undefined || globalThis.WheelEvent === undefined) {
        browserSupportOutput.textContent = 'Not supported';
        updateStatus('Mouse events are not supported in this browser.', 'danger');
        captureSurface.removeAttribute('tabindex');
        return;
    }

    browserSupportOutput.textContent = 'Supported';
    captureSurface.addEventListener('pointerdown', handleSideButtonPointerDown, { capture: true });
    captureSurface.addEventListener('mousedown', handleMouseDown);
    captureSurface.addEventListener('mousemove', handleMouseMove);
    captureSurface.addEventListener('mouseup', preventBrowserAction);
    captureSurface.addEventListener('wheel', handleWheel, { passive: false });
    captureSurface.addEventListener('contextmenu', preventBrowserAction);
    captureSurface.addEventListener('click', preventSideButtonBrowserAction, { capture: true });
    captureSurface.addEventListener('auxclick', preventSideButtonBrowserAction, { capture: true });
    captureSurface.addEventListener('dblclick', function(event) {
        event.preventDefault();
        doubleClicks += 1;
        doubleClicksOutput.textContent = doubleClicks;
        markInput('Double click');
    });
    globalThis.addEventListener('pointerup', handleSideButtonPointerUp, { capture: true });
    globalThis.addEventListener('pointercancel', releaseAllButtons, { capture: true });
    globalThis.addEventListener('mouseup', releaseButtons);
    globalThis.addEventListener('blur', releaseAllButtons);
    document.addEventListener('visibilitychange', function() {
        if (document.hidden) {
            releaseAllButtons();
        }
    });
    document.getElementById('mouse-reset').addEventListener('click', resetTester);
    resetTester();
});
