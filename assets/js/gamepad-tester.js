function getGamepadColorScheme() {
    return document.documentElement.dataset.bsTheme === 'dark' ? 'White' : 'Black';
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
    const firefoxSwitchWarning = document.getElementById('firefox-switch-warning');
    const gamepadAssetBasePath = `https://cdn.jsdelivr.net/npm/@lizardbyte/gamepad-helper@${gamepadHelperVersion}/assets/img/gamepads/`;
    const gamepadVisualizer = gamepadHelper.createVisualizer(
        document.getElementById('controller-visual'),
        {
            assetBasePath: gamepadAssetBasePath,
            colorScheme: getGamepadColorScheme()
        }
    );

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
                    gamepadVisualizer.setColorScheme(getGamepadColorScheme());
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
        const hasFirefoxSwitchMappingIssue = Object.values(gamepads).some(gamepad =>
            gamepadHelper.getCompatibilityIssues(gamepad).some(issue =>
                issue.code === 'firefox-switch-gamepad-mapping'
            )
        );
        gamepadTester.classList.toggle('has-gamepad', hasGamepads);
        firefoxSwitchWarning.hidden = !hasFirefoxSwitchMappingIssue;

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

    function initGamepadVisual() {
        const gamepad = activeGamepadIndex === null
            ? null
            : navigator.getGamepads()[activeGamepadIndex];
        gamepadVisualizer.mount(gamepad);
    }

    // Initialize gamepad button UI elements
    function initGamepadButtons() {
        const buttonsContainer = document.getElementById('buttons-container');
        buttonsContainer.innerHTML = '';

        if (activeGamepadIndex === null) return;

        const gamepad = navigator.getGamepads()[activeGamepadIndex];
        if (!gamepad) return;

        const controllerType = gamepadHelper.detectControllerType(gamepad.id);

        const colorScheme = getGamepadColorScheme();

        for (let i = 0; i < gamepad.buttons.length; i++) {
            const buttonName = gamepadHelper.getButtonName(controllerType, i);
            const buttonImagePath = gamepadHelper.getButtonImagePath(
                controllerType,
                i,
                gamepadAssetBasePath,
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

    function updateTextContent(elementId, value) {
        const element = document.getElementById(elementId);
        const text = String(value);

        if (element.textContent !== text) {
            element.textContent = text;
        }
    }

    // Update the gamepad info function to include vibration status
    function updateGamepadInfo(gamepad) {
        const gamepadInfo = gamepadHelper.getGamepadInfo(gamepad.id);
        const vibrationCapabilities = gamepadHelper.getVibrationCapabilities(gamepad);

        updateTextContent('gamepad-id', gamepad.id);
        updateTextContent('gamepad-index', gamepad.index);
        updateTextContent('gamepad-connected', gamepad.connected);
        updateTextContent('gamepad-mapping', gamepad.mapping || 'No mapping');
        updateTextContent('gamepad-buttons-count', gamepad.buttons.length);
        updateTextContent('gamepad-axes-count', gamepad.axes.length);
        updateTextContent('gamepad-type', gamepadInfo.type);
        updateTextContent('gamepad-name', gamepadInfo.name);

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
            gamepadVisualizer.update(gamepad);
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
