function createDPadButtonVisuals(centerX, centerY) {
    return [
        { index: 12, tag: 'rect', attributes: { x: centerX - 50, y: centerY - 150, width: 100, height: 115, rx: 18 } },
        { index: 13, tag: 'rect', attributes: { x: centerX - 50, y: centerY + 40, width: 100, height: 115, rx: 18 } },
        { index: 14, tag: 'rect', attributes: { x: centerX - 155, y: centerY - 50, width: 115, height: 100, rx: 18 } },
        { index: 15, tag: 'rect', attributes: { x: centerX + 40, y: centerY - 50, width: 115, height: 100, rx: 18 } }
    ];
}

document.addEventListener('DOMContentLoaded', function() {
    const gamepadHelper = new GamepadHelper()
    const gamepadHelperVersion = globalThis.gamepadHelperVersion;
    let gamepads = {};
    let activeGamepadIndex = null;
    let animationFrameId = null;
    const gamepadSelector = document.getElementById('gamepad-selector');
    const gamepadSelectorContainer = document.getElementById('gamepad-selector-container');
    const gamepadInfoSection = document.getElementById('gamepad-info');
    const gamepadTester = document.getElementById('GamepadTester');
    const gamepadStatus = document.getElementById('gamepad-status');
    const gamepadStatusMessage = document.getElementById('gamepad-status-message');
    let gamepadVisualButtons = new Map();
    let gamepadVisualSticks = [];
    let gamepadVisualTriggers = new Map();

    const svgNamespace = 'http://www.w3.org/2000/svg';
    const gamepadAssetBasePath = `https://cdn.jsdelivr.net/npm/@lizardbyte/gamepad-helper@${gamepadHelperVersion}/assets/img/gamepads/`;
    const gamepadVisualConfigs = {
        xbox: {
            viewBox: '1050 450 2050 1350',
            imagePaths: {
                Black: 'xbox/Controller Images/Solid/Solid Black 4k.svg',
                White: 'xbox/Controller Images/Solid/Solid White SVG.svg'
            },
            buttons: [
                { index: 0, tag: 'circle', attributes: { cx: 2491, cy: 925, r: 70 } },
                { index: 1, tag: 'circle', attributes: { cx: 2606, cy: 811, r: 70 } },
                { index: 2, tag: 'circle', attributes: { cx: 2376, cy: 810, r: 70 } },
                { index: 3, tag: 'circle', attributes: { cx: 2491, cy: 695, r: 70 } },
                { index: 4, tag: 'path', attributes: { d: 'M1443,623.26s43.32-137.59,277-140.76c0,0,17.41,2.5,22.55,6.65,0,0,30.3,30.18,49.07,33.29C1791.65,522.44,1582.23,499.75,1443,623.26Z' } },
                { index: 5, tag: 'path', attributes: { d: 'M2653.21,623.26s-43.32-137.59-277-140.76c0,0-17.4,2.5-22.55,6.65,0,0-30.29,30.18-49.07,33.29C2304.6,522.44,2514,499.75,2653.21,623.26Z' } },
                { index: 8, tag: 'circle', attributes: { cx: 1922, cy: 808, r: 52 } },
                { index: 9, tag: 'circle', attributes: { cx: 2173, cy: 808, r: 52 } },
                { index: 10, tag: 'circle', attributes: { cx: 1602, cy: 816, r: 105 } },
                { index: 11, tag: 'circle', attributes: { cx: 2276, cy: 1073, r: 105 } },
                ...createDPadButtonVisuals(1816, 1091),
                { index: 16, tag: 'circle', attributes: { cx: 2049, cy: 637, r: 65 } }
            ],
            sticks: [
                { buttonIndex: 10, axes: [0, 1], x: 1602, y: 816, range: 80 },
                { buttonIndex: 11, axes: [2, 3], x: 2276, y: 1073, range: 80 }
            ]
        },
        playstation: {
            viewBox: '950 400 2200 1350',
            imagePaths: {
                Black: 'playstation/Controller Images/Solid/Solid Black SVG.svg',
                White: 'playstation/Controller Images/Solid/Solid White SVG.svg'
            },
            buttons: [
                { index: 0, tag: 'circle', attributes: { cx: 2668, cy: 966, r: 72 } },
                { index: 1, tag: 'circle', attributes: { cx: 2815, cy: 819, r: 72 } },
                { index: 2, tag: 'circle', attributes: { cx: 2521, cy: 819, r: 72 } },
                { index: 3, tag: 'circle', attributes: { cx: 2668, cy: 672, r: 72 } },
                { index: 4, tag: 'path', attributes: { d: 'M1306.92,530.22v-24s64.45-44.31,142-52.75,109.72,5,109.72,5a57.45,57.45,0,0,1,4.74,28.49S1402.82,497.78,1306.92,530.22Z' } },
                { index: 5, tag: 'path', attributes: { d: 'M2787.64,530.22v-24s-64.45-44.31-142-52.75-109.71,5-109.71,5a57.52,57.52,0,0,0-4.75,28.49S2691.74,497.78,2787.64,530.22Z' } },
                { index: 8, tag: 'rect', attributes: { x: 1545, y: 530, width: 80, height: 145, rx: 35, transform: 'rotate(-8 1585 602)' } },
                { index: 9, tag: 'rect', attributes: { x: 2470, y: 530, width: 80, height: 145, rx: 35, transform: 'rotate(8 2510 602)' } },
                { index: 10, tag: 'circle', attributes: { cx: 1724, cy: 1094, r: 115 } },
                { index: 11, tag: 'circle', attributes: { cx: 2370, cy: 1093, r: 115 } },
                ...createDPadButtonVisuals(1425, 819),
                { index: 16, tag: 'circle', attributes: { cx: 2048, cy: 1094, r: 70 } },
                { index: 17, tag: 'rect', attributes: { x: 1650, y: 465, width: 795, height: 480, rx: 80 } }
            ],
            sticks: [
                { buttonIndex: 10, axes: [0, 1], x: 1724, y: 1094, range: 85 },
                { buttonIndex: 11, axes: [2, 3], x: 2370, y: 1093, range: 85 }
            ]
        },
        switch: {
            viewBox: '1050 450 2050 1350',
            imagePaths: {
                Black: 'switch/Controller Images/Pro Controller/Solid/Pro Controller Solid Black SVG.svg',
                White: 'switch/Controller Images/Pro Controller/Solid/Pro Controller Solid White SVG.svg'
            },
            buttons: [
                { index: 0, tag: 'circle', attributes: { cx: 2501, cy: 956, r: 70 } },
                { index: 1, tag: 'circle', attributes: { cx: 2637, cy: 841, r: 70 } },
                { index: 2, tag: 'circle', attributes: { cx: 2371, cy: 841, r: 70 } },
                { index: 3, tag: 'circle', attributes: { cx: 2501, cy: 725, r: 70 } },
                { index: 4, tag: 'path', attributes: { d: 'M1404.59,610.8c43.07-43.91,147.53-67.61,260.35-80.32l5.36-.59.62-.07c39.26-4.29,79.4-7.26,118.24-9.31C1759,511,1719,487.24,1719,487.24l-20-3.16c-210.46-1.59-285.89,86-285.89,86l-28.81,52.62A132.91,132.91,0,0,0,1404.59,610.8Z' } },
                { index: 5, tag: 'path', attributes: { d: 'M2424.51,529.82l.63.07,5.36.59c112.82,12.71,217.28,36.41,260.35,80.32a135.13,135.13,0,0,0,22.22,12.74l-29.3-53.49s-75.43-87.56-285.89-86l-20,3.16s-40.23,23.87-70.39,33.34C2345.93,522.63,2385.65,525.57,2424.51,529.82Z' } },
                { index: 8, tag: 'circle', attributes: { cx: 1837, cy: 711, r: 50 } },
                { index: 9, tag: 'circle', attributes: { cx: 2260, cy: 711, r: 50 } },
                { index: 10, tag: 'circle', attributes: { cx: 1578, cy: 841, r: 105 } },
                { index: 11, tag: 'circle', attributes: { cx: 2275, cy: 1072, r: 105 } },
                ...createDPadButtonVisuals(1785, 1072),
                { index: 16, tag: 'circle', attributes: { cx: 2172, cy: 841, r: 55 } },
                { index: 17, tag: 'rect', attributes: { x: 1876, y: 791, width: 105, height: 105, rx: 18 } }
            ],
            sticks: [
                { buttonIndex: 10, axes: [0, 1], x: 1578, y: 841, range: 80 },
                { buttonIndex: 11, axes: [2, 3], x: 2275, y: 1072, range: 80 }
            ]
        }
    };

    // Check if the Gamepad API is supported
    if (!gamepadHelper.isSupported()) {
        updateStatus('The Gamepad API is not supported in this browser.', 'danger');
        return;
    }

    // Watch for theme changes to update button images
    const themeObserver = new MutationObserver(function(mutations) {
        mutations.forEach(function(mutation) {
            if (mutation.type === 'attributes' && mutation.attributeName === 'data-bs-theme') {
                // Theme changed, reinitialize buttons with a new color scheme
                if (activeGamepadIndex !== null) {
                    initGamepadButtons();
                    initGamepadVisual();
                }
            }
        });
    });

    // Start observing theme changes on the document element
    themeObserver.observe(document.documentElement, {
        attributes: true,
        attributeFilter: ['data-bs-theme']
    });

    // Setup gamepad event listeners
    globalThis.addEventListener("gamepadconnected", function(e) {
        gamepads[e.gamepad.index] = e.gamepad;

        // Always activate the newly connected gamepad
        activeGamepadIndex = e.gamepad.index;

        updateGamepadSelector(); // This will highlight the active card
        updateStatus(`Gamepad ${e.gamepad.id} connected`);

        // Start the loop if it's not already running
        if (!animationFrameId) {
            startGamepadLoop();
        }
    });

    globalThis.addEventListener("gamepaddisconnected", function(e) {
        delete gamepads[e.gamepad.index];

        // If the active gamepad was disconnected
        if (activeGamepadIndex === e.gamepad.index) {
            activeGamepadIndex = null;

            // If there are other gamepads, select the first one
            const remainingIndices = Object.keys(gamepads);
            if (remainingIndices.length > 0) {
                activeGamepadIndex = Number.parseInt(remainingIndices[0]);
            } else {
                stopGamepadLoop();
            }
        }

        updateGamepadSelector();
        updateStatus(`Gamepad ${e.gamepad.id} disconnected`);
    });

    // Event delegation for gamepad selector cards
    gamepadSelector.addEventListener('click', function(e) {
        const card = e.target.closest('.gamepad-selector-card');
        if (card) {
            activeGamepadIndex = Number.parseInt(card.dataset.index);
            updateGamepadSelector(); // Re-render to update the active state
            initGamepadButtons();
            initGamepadAxes();
        }
    });

    // Event listeners for vibration controls
    document.getElementById('vibrate-btn').addEventListener('click', function() {
        vibrateGamepad();
    });

    document.getElementById('stop-vibration-btn').addEventListener('click', function() {
        stopVibration();
    });

    // Update gamepad selector buttons
    function updateGamepadSelector() {
        gamepadSelector.innerHTML = '';

        const gamepadIndices = Object.keys(gamepads);
        const hasGamepads = gamepadIndices.length > 0;
        gamepadTester.classList.toggle('has-gamepad', hasGamepads);

        if (hasGamepads) {
            gamepadSelectorContainer.style.removeProperty('display');
            gamepadInfoSection.style.removeProperty('display');

            gamepadIndices.forEach(index => {
                const card = document.createElement('button');
                card.className = 'gamepad-selector-card';
                card.dataset.index = index;
                card.type = 'button';

                const isActive = activeGamepadIndex !== null && Number.parseInt(index) === activeGamepadIndex;
                card.setAttribute('aria-pressed', isActive.toString());

                const gamepadInfo = gamepads[index];
                const typeInfo = gamepadHelper.getGamepadInfo(gamepadInfo.id);
                card.setAttribute('aria-label', `${isActive ? 'Active controller' : 'Select controller'}: ${typeInfo.name}`);

                card.innerHTML = `
                    <span class="gamepad-selector-icon" aria-hidden="true"><i class="fas fa-gamepad"></i></span>
                    <span>
                        <span class="gamepad-selector-name">${typeInfo.name}</span>
                        <span class="gamepad-selector-meta">Index ${index} · ${gamepadInfo.buttons.length} buttons · ${gamepadInfo.axes.length} axes</span>
                    </span>
                    ${isActive ? '<span class="gamepad-selector-active">Active</span>' : '<span></span>'}
                `;

                gamepadSelector.appendChild(card);
            });

            // Initialize controls for the selected gamepad
            if (activeGamepadIndex !== null) {
                initGamepadButtons();
                initGamepadAxes();
                initGamepadVisual();
            }
        } else {
            gamepadSelectorContainer.style.display = 'none';
            gamepadInfoSection.style.display = 'none';
        }
    }

    function createSvgElement(tag, attributes = {}) {
        const element = document.createElementNS(svgNamespace, tag);

        Object.entries(attributes).forEach(([name, value]) => {
            element.setAttribute(name, value);
        });

        return element;
    }

    function showVisualUnavailable(visualContainer) {
        const message = document.createElement('span');
        message.className = 'text-muted';
        message.textContent = 'A visual is not available for this controller.';
        visualContainer.replaceChildren(message);
    }

    function getGamepadVisualImagePath(config) {
        const isDarkTheme = document.documentElement.dataset.bsTheme === 'dark';
        const colorScheme = isDarkTheme ? 'White' : 'Black';
        const encodedPath = config.imagePaths[colorScheme]
            .split('/')
            .map(pathPart => encodeURIComponent(pathPart))
            .join('/');

        return gamepadAssetBasePath + encodedPath;
    }

    function createTriggerVisual(controllerType, buttonIndex, colorScheme) {
        const buttonName = gamepadHelper.getButtonName(controllerType, buttonIndex);
        const trigger = document.createElement('div');
        trigger.className = 'gamepad-trigger-visual';
        trigger.setAttribute('role', 'progressbar');
        trigger.setAttribute('aria-label', `${buttonName} trigger pressure`);
        trigger.setAttribute('aria-valuemin', '0');
        trigger.setAttribute('aria-valuemax', '1');
        trigger.setAttribute('aria-valuenow', '0');

        const fill = document.createElement('span');
        fill.className = 'gamepad-trigger-fill';
        trigger.appendChild(fill);

        const buttonImagePath = gamepadHelper.getButtonImagePath(
            controllerType,
            buttonIndex,
            gamepadAssetBasePath,
            colorScheme
        );
        const buttonImage = document.createElement('img');
        buttonImage.className = 'gamepad-trigger-image';
        buttonImage.src = buttonImagePath;
        buttonImage.alt = buttonName;
        trigger.appendChild(buttonImage);

        const fallback = document.createElement('span');
        fallback.className = 'gamepad-trigger-name';
        fallback.textContent = buttonName;
        trigger.appendChild(fallback);

        buttonImage.addEventListener('error', function() {
            buttonImage.classList.add('d-none');
            fallback.classList.add('visible');
        }, { once: true });

        const value = document.createElement('span');
        value.className = 'gamepad-trigger-value';
        value.textContent = '0.00';
        trigger.appendChild(value);

        gamepadVisualTriggers.set(buttonIndex, { element: trigger, value });
        return trigger;
    }

    function initGamepadVisual() {
        const visualContainer = document.getElementById('controller-visual');
        gamepadVisualButtons = new Map();
        gamepadVisualSticks = [];
        gamepadVisualTriggers = new Map();
        visualContainer.replaceChildren();

        if (activeGamepadIndex === null) {
            showVisualUnavailable(visualContainer);
            return;
        }

        const gamepad = navigator.getGamepads()[activeGamepadIndex];
        if (!gamepad) {
            showVisualUnavailable(visualContainer);
            return;
        }

        const gamepadInfo = gamepadHelper.getGamepadInfo(gamepad.id);
        const config = gamepadVisualConfigs[gamepadInfo.type];
        if (!config) {
            showVisualUnavailable(visualContainer);
            return;
        }

        const isDarkTheme = document.documentElement.dataset.bsTheme === 'dark';
        const colorScheme = isDarkTheme ? 'White' : 'Black';
        visualContainer.appendChild(createTriggerVisual(gamepadInfo.type, 6, colorScheme));

        const visual = createSvgElement('svg', {
            class: 'gamepad-visual-svg',
            viewBox: config.viewBox,
            role: 'img',
            'aria-label': `${gamepadInfo.name} input visual`
        });
        const title = createSvgElement('title');
        title.textContent = `${gamepadInfo.name} input visual`;
        visual.appendChild(title);

        const controllerImage = createSvgElement('image', {
            href: getGamepadVisualImagePath(config),
            width: 4096,
            height: 2160,
            preserveAspectRatio: 'xMidYMid meet'
        });
        controllerImage.addEventListener('error', function() {
            showVisualUnavailable(visualContainer);
        }, { once: true });
        visual.appendChild(controllerImage);

        config.buttons.forEach(button => {
            const control = createSvgElement(button.tag, button.attributes);
            control.classList.add('gamepad-visual-control');
            control.dataset.buttonIndex = button.index;
            visual.appendChild(control);
            gamepadVisualButtons.set(button.index, control);
        });

        config.sticks.forEach(stick => {
            const indicator = createSvgElement('circle', {
                class: 'gamepad-stick-indicator',
                cx: stick.x,
                cy: stick.y,
                r: 28
            });
            visual.appendChild(indicator);
            gamepadVisualSticks.push({
                ...stick,
                buttonElement: gamepadVisualButtons.get(stick.buttonIndex),
                indicator
            });
        });

        visualContainer.appendChild(visual);
        visualContainer.appendChild(createTriggerVisual(gamepadInfo.type, 7, colorScheme));
    }

    // Initialize gamepad button UI elements
    function initGamepadButtons() {
        const buttonsContainer = document.getElementById('buttons-container');
        buttonsContainer.innerHTML = '';

        if (activeGamepadIndex === null) return;

        const gamepad = navigator.getGamepads()[activeGamepadIndex];
        if (!gamepad) return;

        const controllerType = gamepadHelper.detectControllerType(gamepad.id);

        // Detect the current theme - use Black icons for the light theme, White icons for the dark theme
        const isDarkTheme = document.documentElement.dataset.bsTheme === 'dark';
        const colorScheme = isDarkTheme ? 'White' : 'Black';

        for (let i = 0; i < gamepad.buttons.length; i++) {
            const buttonName = gamepadHelper.getButtonName(controllerType, i);
            const buttonImagePath = gamepadHelper.getButtonImagePath(
                controllerType,
                i,
                `https://cdn.jsdelivr.net/npm/@lizardbyte/gamepad-helper@${gamepadHelperVersion}/assets/img/gamepads/`,
                colorScheme
            );

            const buttonDiv = document.createElement('div');
            buttonDiv.className = 'circular-button';
            buttonDiv.id = `button-${i}`;
            buttonDiv.setAttribute('aria-label', `${buttonName} button value`);
            buttonDiv.setAttribute('aria-valuemax', '1');
            buttonDiv.setAttribute('aria-valuemin', '0');
            buttonDiv.setAttribute('aria-valuenow', '0');
            buttonDiv.setAttribute('role', 'progressbar');

            // Create the progress elements
            const progressLeft = document.createElement('span');
            progressLeft.className = 'progress-left';
            progressLeft.innerHTML = `<span class="progress-bar" id="progress-bar-left-${i}"></span>`;

            const progressRight = document.createElement('span');
            progressRight.className = 'progress-right';
            progressRight.innerHTML = `<span class="progress-bar" id="progress-bar-right-${i}"></span>`;

            // Create the button content
            const buttonContent = document.createElement('div');
            buttonContent.className = 'button-content';

            // Add either image with fallback text or just text
            if (buttonImagePath) {
                buttonContent.innerHTML = `
                <div class="button-image-container">
                    <img src="${buttonImagePath}" alt="${buttonName}" class="button-image" onerror="this.style.display='none'; this.nextElementSibling.style.display='block';">
                    <div class="button-name" style="display: none;">${buttonName}</div>
                </div>
                <div class="button-value" id="button-value-${i}">0.00</div>
            `;
            } else {
                buttonContent.innerHTML = `
                <div class="button-name">${buttonName}</div>
                <div class="button-value" id="button-value-${i}">0.00</div>
            `;
            }

            buttonDiv.appendChild(progressLeft);
            buttonDiv.appendChild(progressRight);
            buttonDiv.appendChild(buttonContent);

            buttonsContainer.appendChild(buttonDiv);
        }
    }

    // Initialize gamepad axes UI elements
    function initGamepadAxes() {
        const axesContainer = document.getElementById('axes-container');
        axesContainer.innerHTML = '';

        if (activeGamepadIndex === null) return;

        const gamepad = navigator.getGamepads()[activeGamepadIndex];
        if (!gamepad) return;

        const controllerType = gamepadHelper.detectControllerType(gamepad.id);

        for (let i = 0; i < gamepad.axes.length; i++) {
            const axis = document.createElement('div');
            axis.className = 'gamepad-axis';

            const axisName = gamepadHelper.getAxisName(controllerType, i);

            axis.innerHTML = `
                <div class="gamepad-axis-header">
                    <span>${axisName}</span>
                    <output id="axis-value-${i}" class="gamepad-axis-value">0.00</output>
                </div>
                <div id="axis-meter-${i}" class="gamepad-axis-track" role="progressbar"
                     aria-label="${axisName}" aria-valuenow="0" aria-valuemin="-1" aria-valuemax="1">
                    <span class="gamepad-axis-center" aria-hidden="true"></span>
                    <span id="axis-progress-${i}" class="gamepad-axis-fill" aria-hidden="true"></span>
                </div>
            `;

            axesContainer.appendChild(axis);
        }
    }

    // Update gamepad status message
    function updateStatus(message, status) {
        const resolvedStatus = status || (Object.keys(gamepads).length > 0 ? 'success' : 'warning');
        gamepadStatusMessage.textContent = message;
        gamepadStatus.classList.remove('alert-success', 'alert-warning', 'alert-danger');
        gamepadStatus.classList.add(`alert-${resolvedStatus}`);
    }

    // Update the gamepad info function to include vibration status
    function updateGamepadInfo(gamepad) {
        const gamepadInfo = gamepadHelper.getGamepadInfo(gamepad.id);
        const vibrationCapabilities = gamepadHelper.getVibrationCapabilities(gamepad);

        document.getElementById('gamepad-id').textContent = gamepad.id;
        document.getElementById('gamepad-index').textContent = gamepad.index;
        document.getElementById('gamepad-connected').textContent = gamepad.connected;
        document.getElementById('gamepad-mapping').textContent = gamepad.mapping || 'No mapping';
        document.getElementById('gamepad-buttons-count').textContent = gamepad.buttons.length;
        document.getElementById('gamepad-axes-count').textContent = gamepad.axes.length;
        document.getElementById('gamepad-type').textContent = gamepadInfo.type;
        document.getElementById('gamepad-name').textContent = gamepadInfo.name;

        // Update vibration controls based on capabilities
        updateVibrationControls(vibrationCapabilities);
    }

    // Update vibration controls based on device capabilities
    function updateVibrationControls(vibrationCapabilities) {
        const vibrationStatus = document.getElementById('vibration-status');
        const dualRumbleControls = document.getElementById('dual-rumble-controls');
        const simpleVibrationControls = document.getElementById('simple-vibration-controls');
        const vibrationDurationControls = document.getElementById('vibration-duration-controls');
        const vibrationButtons = document.getElementById('vibration-buttons');
        const vibrationUnsupported = document.getElementById('vibration-unsupported');

        // Hide all controls first
        dualRumbleControls.classList.add('d-none');
        simpleVibrationControls.classList.add('d-none');
        vibrationDurationControls.classList.add('d-none');
        vibrationButtons.classList.add('d-none');
        vibrationUnsupported.classList.add('d-none');

        if (!vibrationCapabilities.supported) {
            vibrationStatus.textContent = '';
            vibrationUnsupported.classList.remove('d-none');
            return;
        }

        // Show information about the actuator type
        const capabilityBadge = document.createElement('span');
        capabilityBadge.className = 'gamepad-capability-badge';
        capabilityBadge.textContent = 'Vibration supported';

        const capabilityType = document.createElement('span');
        capabilityType.className = 'gamepad-capability-type';
        capabilityType.textContent = vibrationCapabilities.type;

        vibrationStatus.replaceChildren(capabilityBadge, capabilityType);
        vibrationButtons.classList.remove('d-none');

        // Show appropriate controls based on an actuator type
        if (vibrationCapabilities.type === 'dual-rumble') {
            dualRumbleControls.classList.remove('d-none');
        } else {
            simpleVibrationControls.classList.remove('d-none');
        }

        // Show duration controls for all types
        vibrationDurationControls.classList.remove('d-none');
    }

    // Update button UI states
    function updateButtons(gamepad) {
        for (let i = 0; i < gamepad.buttons.length; i++) {
            const button = gamepad.buttons[i];
            const buttonElement = document.getElementById(`button-${i}`);
            const buttonValueElement = document.getElementById(`button-value-${i}`);
            const progressBarLeftElement = document.getElementById(`progress-bar-left-${i}`);
            const progressBarRightElement = document.getElementById(`progress-bar-right-${i}`);

            if (buttonElement && progressBarLeftElement && progressBarRightElement && buttonValueElement) {
                const value = button.value;
                const isPressed = button.pressed || value > 0.1;

                // Update the value display
                buttonValueElement.textContent = value.toFixed(2);
                buttonElement.setAttribute('aria-valuenow', value.toFixed(2));

                // Calculate rotation degrees based on value (0 to 1)
                // For a full circle: right part goes from 0 to 180 degrees, left part from 0 to 180 degrees
                const degrees = value * 360;

                // Reset transforms
                progressBarRightElement.style.transform = 'rotate(0deg)';
                progressBarLeftElement.style.transform = 'rotate(0deg)';

                if (degrees <= 180) {
                    // Only the right side rotates for the first half
                    progressBarRightElement.style.transform = `rotate(${degrees}deg)`;
                } else {
                    // The right side is at full rotation, the left side rotates for the remainder
                    progressBarRightElement.style.transform = 'rotate(180deg)';
                    progressBarLeftElement.style.transform = `rotate(${degrees - 180}deg)`;
                }

                // Add/remove the active class based on the button state
                if (isPressed) {
                    buttonElement.classList.add('active');
                } else {
                    buttonElement.classList.remove('active');
                }
            }
        }
    }

    function updateGamepadVisual(gamepad) {
        for (let i = 0; i < gamepad.buttons.length; i++) {
            const control = gamepadVisualButtons.get(i);
            if (!control) continue;

            const button = gamepad.buttons[i];
            const isActive = button.pressed || button.value > 0.1;
            const opacity = Math.max(0.35, button.value * 0.9);
            control.classList.toggle('active', isActive);
            control.style.fillOpacity = isActive ? opacity.toFixed(2) : '0';
        }

        gamepadVisualTriggers.forEach((trigger, buttonIndex) => {
            const button = gamepad.buttons[buttonIndex];
            const value = Math.max(0, Math.min(1, button?.value || 0));
            const isActive = Boolean(button?.pressed) || value > 0.1;
            trigger.element.classList.toggle('active', isActive);
            trigger.element.style.setProperty('--gamepad-trigger-value', `${(value * 100).toFixed(0)}%`);
            trigger.element.setAttribute('aria-valuenow', value.toFixed(2));
            trigger.value.textContent = value.toFixed(2);
        });

        gamepadVisualSticks.forEach(stick => {
            const horizontalValue = Math.max(-1, Math.min(1, gamepad.axes[stick.axes[0]] || 0));
            const verticalValue = Math.max(-1, Math.min(1, gamepad.axes[stick.axes[1]] || 0));
            const transform = `translate(${(horizontalValue * stick.range).toFixed(1)} ${(verticalValue * stick.range).toFixed(1)})`;
            stick.buttonElement.setAttribute('transform', transform);
            stick.indicator.setAttribute('transform', transform);
        });
    }

    // Update axes UI states
    function updateAxes(gamepad) {
        for (let i = 0; i < gamepad.axes.length; i++) {
            const axisValue = gamepad.axes[i];
            const axisValueElement = document.getElementById(`axis-value-${i}`);
            const axisProgressElement = document.getElementById(`axis-progress-${i}`);
            const axisMeterElement = document.getElementById(`axis-meter-${i}`);

            if (axisValueElement && axisProgressElement && axisMeterElement) {
                // Display the value
                axisValueElement.textContent = axisValue.toFixed(2);
                axisMeterElement.setAttribute('aria-valuenow', axisValue.toFixed(2));

                // Fill away from the zero marker in the direction of travel.
                const progressWidth = Math.abs(axisValue) * 50;
                axisProgressElement.style.left = axisValue < 0 ? `${50 - progressWidth}%` : '50%';
                axisProgressElement.style.width = `${progressWidth}%`;

                // Change color based on the direction
                if (axisValue > 0.1) {
                    axisProgressElement.classList.remove('is-negative');
                    axisProgressElement.classList.add('is-positive');
                } else if (axisValue < -0.1) {
                    axisProgressElement.classList.remove('is-positive');
                    axisProgressElement.classList.add('is-negative');
                } else {
                    axisProgressElement.classList.remove('is-positive', 'is-negative');
                }
            }
        }

        // Update stick visualizations
        updateStickVisuals(gamepad);
    }

    // Update sticks visual representation
    function updateStickVisuals(gamepad) {
        const leftStick = document.getElementById('left-stick-position');
        const rightStick = document.getElementById('right-stick-position');

        if (gamepad.axes.length >= 2 && leftStick) {
            const range = (leftStick.parentElement.clientWidth / 2) - (leftStick.clientWidth / 2) - 2;
            const x = gamepad.axes[0] * range;
            const y = gamepad.axes[1] * range;

            // Center position is 50%, then offset by the calculated amounts
            leftStick.style.left = `calc(50% + ${x}px)`;
            leftStick.style.top = `calc(50% + ${y}px)`;
        }

        if (gamepad.axes.length >= 4 && rightStick) {
            const range = (rightStick.parentElement.clientWidth / 2) - (rightStick.clientWidth / 2) - 2;
            const x = gamepad.axes[2] * range;
            const y = gamepad.axes[3] * range;

            rightStick.style.left = `calc(50% + ${x}px)`;
            rightStick.style.top = `calc(50% + ${y}px)`;
        }
    }

    // Update raw data display
    function updateRawData(gamepad) {
        const rawDataElement = document.getElementById('raw-data');

        const buttons = Array.from(gamepad.buttons).map((button, index) => {
            return `Button ${index}: { pressed: ${button.pressed}, value: ${button.value.toFixed(2)} }`;
        });

        const axes = Array.from(gamepad.axes).map((axis, index) => {
            return `Axis ${index}: ${axis.toFixed(2)}`;
        });

        rawDataElement.textContent = `Gamepad: ${gamepad.id}\nType: ${gamepadHelper.detectControllerType(gamepad.id)}\n\nButtons:\n${buttons.join('\n')}\n\nAxes:\n${axes.join('\n')}`;
    }

    // Event listeners for vibration controls
    document.getElementById('vibration-weak').addEventListener('input', function() {
        document.getElementById('weak-value').textContent = this.value;
    });

    document.getElementById('vibration-strong').addEventListener('input', function() {
        document.getElementById('strong-value').textContent = this.value;
    });

    document.getElementById('vibration-magnitude').addEventListener('input', function() {
        document.getElementById('magnitude-value').textContent = this.value;
    });

    document.getElementById('vibration-duration').addEventListener('input', function() {
        document.getElementById('duration-value').textContent = this.value;
    });

    // Vibrate the gamepad with appropriate parameters
    function vibrateGamepad() {
        if (activeGamepadIndex !== null) {
            const gamepad = navigator.getGamepads()[activeGamepadIndex];
            if (!gamepad) return;

            const vibrationCapabilities = gamepadHelper.getVibrationCapabilities(gamepad);
            if (!vibrationCapabilities.supported) return;

            const duration = Number.parseInt(document.getElementById('vibration-duration').value);
            let vibrationOptions = { duration };

            if (vibrationCapabilities.type === 'dual-rumble') {
                vibrationOptions.weakMagnitude = Number.parseFloat(document.getElementById('vibration-weak').value);
                vibrationOptions.strongMagnitude = Number.parseFloat(document.getElementById('vibration-strong').value);
            } else {
                const magnitude = Number.parseFloat(document.getElementById('vibration-magnitude').value);
                vibrationOptions.weakMagnitude = magnitude;
                vibrationOptions.strongMagnitude = magnitude;
                vibrationOptions.magnitude = magnitude;
            }

            gamepadHelper.vibrate(gamepad, vibrationOptions)
                .then(() => console.log('Vibration started'))
                .catch(e => console.error('Vibration error:', e));
        }
    }

    // Stop vibration
    function stopVibration() {
        if (activeGamepadIndex !== null) {
            const gamepad = navigator.getGamepads()[activeGamepadIndex];
            if (gamepad) {
                gamepadHelper.stopVibration(gamepad).catch(e => {
                    console.error('Stop vibration error:', e);
                });
            }
        }
    }

    // Start the gamepad polling loop
    function startGamepadLoop() {
        if (animationFrameId) return;

        // Make sure UI elements are initialized when starting the loop
        if (activeGamepadIndex !== null) {
            initGamepadButtons();
            initGamepadAxes();
            initGamepadVisual();
        }

        gamepadLoop();
    }

    // Stop the gamepad polling loop
    function stopGamepadLoop() {
        if (animationFrameId) {
            cancelAnimationFrame(animationFrameId);
            animationFrameId = null;
        }
    }

    // The main gamepad polling loop
    function gamepadLoop() {
        // Get the latest gamepad state
        const gamepads = navigator.getGamepads();

        if (activeGamepadIndex !== null && gamepads[activeGamepadIndex]) {
            const gamepad = gamepads[activeGamepadIndex];

            // Update all the UI elements
            updateGamepadInfo(gamepad);
            updateButtons(gamepad);
            updateGamepadVisual(gamepad);
            updateAxes(gamepad);
            updateRawData(gamepad);
        }

        animationFrameId = requestAnimationFrame(gamepadLoop);
    }

    // Initial check for already connected gamepads
    const initialGamepads = navigator.getGamepads();
    for (const element of initialGamepads) {
        if (element) {
            gamepads[element.index] = element;
        }
    }

    // If we have gamepads already, activate the first one
    if (Object.keys(gamepads).length > 0) {
        activeGamepadIndex = Number.parseInt(Object.keys(gamepads)[0]);
        updateStatus(`Gamepad ${gamepads[activeGamepadIndex].id} connected`);
        updateGamepadSelector(); // Update after setting activeGamepadIndex
        startGamepadLoop();
    }
});
