document.addEventListener('DOMContentLoaded', function() {
    let gamepads = {};
    let activeGamepadIndex = null;
    let animationFrameId = null;
    let gamepadSelector = document.getElementById('gamepad-selector');
    let gamepadSelectorContainer = document.getElementById('gamepad-selector-container');
    let gamepadInfoSection = document.getElementById('gamepad-info');
    let gamepadStatus = document.getElementById('gamepad-status');

    // Check if the Gamepad API is supported
    if (!GamepadHelper.isSupported()) {
        gamepadStatus.textContent = 'Gamepad API not supported in this browser';
        gamepadStatus.classList.remove('alert-warning');
        gamepadStatus.classList.add('alert-danger');
        return;
    }

    // Initialize button colors for various states
    const buttonColors = {
        'standard': {
            'inactive': 'btn-outline-primary',
            'active': 'btn-primary'
        },
    };

    // Axis names mapping
    const axisNames = [
        'Left Stick X', 'Left Stick Y',
        'Right Stick X', 'Right Stick Y'
    ];

    // Setup gamepad event listeners
    window.addEventListener("gamepadconnected", function(e) {
        gamepads[e.gamepad.index] = e.gamepad;
        updateGamepadSelector();
        updateStatus(`Gamepad ${e.gamepad.id} connected`);

        // If this is the first gamepad, activate it
        if (activeGamepadIndex === null) {
            activeGamepadIndex = e.gamepad.index;
            gamepadSelector.value = activeGamepadIndex;
            startGamepadLoop();
        }
    });

    window.addEventListener("gamepaddisconnected", function(e) {
        delete gamepads[e.gamepad.index];
        updateGamepadSelector();

        updateStatus(`Gamepad ${e.gamepad.id} disconnected`);

        // If the active gamepad was disconnected
        if (activeGamepadIndex === e.gamepad.index) {
            activeGamepadIndex = null;

            // If there are other gamepads, select the first one
            const remainingIndices = Object.keys(gamepads);
            if (remainingIndices.length > 0) {
                activeGamepadIndex = parseInt(remainingIndices[0]);
                gamepadSelector.value = activeGamepadIndex;
            } else {
                stopGamepadLoop();
            }
        }
    });

    // Event listener for gamepad selector change
    gamepadSelector.addEventListener('change', function() {
        activeGamepadIndex = parseInt(this.value);
        initGamepadButtons();
        initGamepadAxes();
    });

    // Event listeners for vibration controls
    document.getElementById('vibrate-btn').addEventListener('click', function() {
        vibrateGamepad();
    });

    document.getElementById('stop-vibration-btn').addEventListener('click', function() {
        stopVibration();
    });

    document.getElementById('vibration-weak').addEventListener('input', function() {
        document.getElementById('weak-value').textContent = this.value;
    });

    document.getElementById('vibration-strong').addEventListener('input', function() {
        document.getElementById('strong-value').textContent = this.value;
    });

    // Update gamepad selector dropdown
    function updateGamepadSelector() {
        gamepadSelector.innerHTML = '';

        const gamepadIndices = Object.keys(gamepads);

        if (gamepadIndices.length > 0) {
            gamepadSelectorContainer.style.display = 'block';
            gamepadInfoSection.style.display = 'flex';

            gamepadIndices.forEach(index => {
                const option = document.createElement('option');
                option.value = index;
                option.textContent = `${gamepads[index].id} (Index: ${index})`;
                gamepadSelector.appendChild(option);
            });

            // Select the active gamepad
            if (activeGamepadIndex !== null) {
                gamepadSelector.value = activeGamepadIndex;
            }

            // Initialize buttons and axes for the selected gamepad
            initGamepadButtons();
            initGamepadAxes();
        } else {
            gamepadSelectorContainer.style.display = 'none';
            gamepadInfoSection.style.display = 'none';
        }
    }

    // Initialize gamepad button UI elements
    function initGamepadButtons() {
        const buttonsContainer = document.getElementById('buttons-container');
        buttonsContainer.innerHTML = '';

        if (activeGamepadIndex === null) return;

        const gamepad = navigator.getGamepads()[activeGamepadIndex];
        if (!gamepad) return;

        const controllerType = GamepadHelper.detectControllerType(gamepad.id);

        for (let i = 0; i < gamepad.buttons.length; i++) {
            const buttonName = GamepadHelper.getButtonName(controllerType, i);

            const buttonDiv = document.createElement('div');
            buttonDiv.className = 'circular-button';
            buttonDiv.id = `button-${i}`;

            buttonDiv.innerHTML = `
            <span class="progress-left">
                <span class="progress-bar" id="progress-bar-left-${i}"></span>
            </span>
            <span class="progress-right">
                <span class="progress-bar" id="progress-bar-right-${i}"></span>
            </span>
            <div class="button-content">
                <div class="button-name">${buttonName}</div>
                <div class="button-value" id="button-value-${i}">0.00</div>
            </div>
        `;

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

        const controllerType = GamepadHelper.detectControllerType(gamepad.id);

        for (let i = 0; i < gamepad.axes.length; i++) {
            const colDiv = document.createElement('div');
            colDiv.className = 'col-md-6 col-lg-3 mb-3';

            const axisName = GamepadHelper.getAxisName(controllerType, i);

            colDiv.innerHTML = `
                <div class="mb-1">${axisName}: <span id="axis-value-${i}">0.00</span></div>
                <div class="progress" style="height: 20px">
                    <div id="axis-progress-${i}" class="progress-bar bg-info" role="progressbar"
                         style="width: 50%;" aria-valuenow="0" aria-valuemin="-1" aria-valuemax="1"></div>
                </div>
            `;

            axesContainer.appendChild(colDiv);
        }
    }

    // Update gamepad status message
    function updateStatus(message) {
        gamepadStatus.textContent = message;

        // Update classes based on if we have a gamepad
        if (Object.keys(gamepads).length > 0) {
            gamepadStatus.classList.remove('alert-warning', 'alert-danger');
            gamepadStatus.classList.add('alert-success');
        } else {
            gamepadStatus.classList.remove('alert-success', 'alert-danger');
            gamepadStatus.classList.add('alert-warning');
        }
    }

    // Update the gamepad info function to include vibration status
    function updateGamepadInfo(gamepad) {
        const gamepadInfo = GamepadHelper.getGamepadInfo(gamepad.id);
        const vibrationCapabilities = GamepadHelper.getVibrationCapabilities(gamepad);

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
        vibrationStatus.innerHTML = `<span class="badge bg-success">Supported</span> Actuator type: <span class="badge bg-secondary">${vibrationCapabilities.type}</span>`;
        vibrationButtons.classList.remove('d-none');

        // Show appropriate controls based on actuator type
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
                    // Right side is at full rotation, left side rotates for the remainder
                    progressBarRightElement.style.transform = 'rotate(180deg)';
                    progressBarLeftElement.style.transform = `rotate(${degrees - 180}deg)`;
                }

                // Add/remove active class based on button state
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

            if (axisValueElement && axisProgressElement) {
                // Display the value
                axisValueElement.textContent = axisValue.toFixed(2);

                // Update the progress bar
                // Convert -1 to 1 to 0 to 100 for the progress bar
                const progressWidth = ((axisValue + 1) / 2) * 100;
                axisProgressElement.style.width = `${progressWidth}%`;

                // Change color based on direction
                if (axisValue > 0.1) {
                    axisProgressElement.classList.remove('bg-info', 'bg-danger');
                    axisProgressElement.classList.add('bg-success');
                } else if (axisValue < -0.1) {
                    axisProgressElement.classList.remove('bg-info', 'bg-success');
                    axisProgressElement.classList.add('bg-danger');
                } else {
                    axisProgressElement.classList.remove('bg-success', 'bg-danger');
                    axisProgressElement.classList.add('bg-info');
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
            // Convert -1 to 1 values to pixel positions
            const x = gamepad.axes[0] * 60; // Scale factor to fit within the circle
            const y = gamepad.axes[1] * 60;

            // Center position is 50%, then offset by the calculated amounts
            leftStick.style.left = `calc(50% + ${x}px)`;
            leftStick.style.top = `calc(50% + ${y}px)`;
        }

        if (gamepad.axes.length >= 4 && rightStick) {
            const x = gamepad.axes[2] * 60;
            const y = gamepad.axes[3] * 60;

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

        rawDataElement.textContent = `Gamepad: ${gamepad.id}\nType: ${GamepadHelper.detectControllerType(gamepad.id)}\n\nButtons:\n${buttons.join('\n')}\n\nAxes:\n${axes.join('\n')}`;
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

            const vibrationCapabilities = GamepadHelper.getVibrationCapabilities(gamepad);
            if (!vibrationCapabilities.supported) return;

            const duration = parseInt(document.getElementById('vibration-duration').value);
            let vibrationOptions = { duration };

            if (vibrationCapabilities.type === 'dual-rumble') {
                vibrationOptions.weakMagnitude = parseFloat(document.getElementById('vibration-weak').value);
                vibrationOptions.strongMagnitude = parseFloat(document.getElementById('vibration-strong').value);
            } else {
                const magnitude = parseFloat(document.getElementById('vibration-magnitude').value);
                vibrationOptions.weakMagnitude = magnitude;
                vibrationOptions.strongMagnitude = magnitude;
                vibrationOptions.magnitude = magnitude;
            }

            GamepadHelper.vibrate(gamepad, vibrationOptions)
                .then(() => console.log('Vibration started'))
                .catch(e => console.error('Vibration error:', e));
        }
    }

    // Stop vibration
    function stopVibration() {
        if (activeGamepadIndex !== null) {
            const gamepad = navigator.getGamepads()[activeGamepadIndex];
            if (gamepad) {
                GamepadHelper.stopVibration(gamepad).catch(e => {
                    console.error('Stop vibration error:', e);
                });
            }
        }
    }

    // Start the gamepad polling loop
    function startGamepadLoop() {
        if (animationFrameId) return;

        // Make sure UI elements are initialized when starting loop
        if (activeGamepadIndex !== null) {
            initGamepadButtons();
            initGamepadAxes();
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
            updateAxes(gamepad);
            updateRawData(gamepad);
        }

        animationFrameId = requestAnimationFrame(gamepadLoop);
    }

    // Initial check for already connected gamepads
    const initialGamepads = navigator.getGamepads();
    for (let i = 0; i < initialGamepads.length; i++) {
        if (initialGamepads[i]) {
            gamepads[initialGamepads[i].index] = initialGamepads[i];
        }
    }

    updateGamepadSelector();

    // If we have gamepads already, start the loop
    if (Object.keys(gamepads).length > 0) {
        activeGamepadIndex = parseInt(Object.keys(gamepads)[0]);
        updateStatus(`Gamepad ${gamepads[activeGamepadIndex].id} connected`);
        startGamepadLoop();
    }
});
