/**
 * @file game-detector.js
 * @brief Módulo frontend para mostrar juegos detectados
 * 
 * Ubicación: src_assets/common/assets/web/game-detector.js
 */

(function() {
    'use strict';

    const API_BASE = window.location.origin;
    const GAME_API = `${API_BASE}/api/games`;

    let detectedGames = [];
    let selectedPlatform = 'all';

    // ===== INICIALIZACIÓN =====

    async function init() {
        try {
            await loadDetectedGames();
            createGameDetectorUI();
            console.log('Game detector initialized');
        } catch (error) {
            console.error('Failed to initialize game detector:', error);
        }
    }

    // ===== API CALLS =====

    async function loadDetectedGames() {
        try {
            const response = await fetch(`${GAME_API}/detected`);
            if (!response.ok) throw new Error('Failed to fetch games');
            detectedGames = await response.json();
        } catch (error) {
            console.error('Error loading games:', error);
            detectedGames = [];
        }
    }

    async function loadPlatformGames(platform) {
        try {
            const response = await fetch(`${GAME_API}/detected/${platform}`);
            if (!response.ok) throw new Error('Failed to fetch platform games');
            detectedGames = await response.json();
        } catch (error) {
            console.error('Error loading platform games:', error);
            detectedGames = [];
        }
    }

    async function getAvailablePlatforms() {
        try {
            const response = await fetch(`${GAME_API}/platforms`);
            if (!response.ok) throw new Error('Failed to fetch platforms');
            return await response.json();
        } catch (error) {
            console.error('Error loading platforms:', error);
            return [];
        }
    }

    // ===== UI CREATION =====

    function createGameDetectorUI() {
        const container = document.querySelector('.container') || document.body;
        
        const section = document.createElement('div');
        section.className = 'game-detector-section';
        section.innerHTML = `
            <div class="game-detector-header">
                <h2>🎮 Juegos Detectados Automáticamente</h2>
                <div class="game-detector-controls">
                    <select id="platform-filter" class="form-control">
                        <option value="all">Todas las plataformas</option>
                    </select>
                    <button id="refresh-games" class="btn btn-primary">
                        🔄 Refrescar
                    </button>
                </div>
            </div>
            <div id="games-grid" class="games-grid"></div>
        `;

        container.insertBefore(section, container.firstChild);

        setupEventListeners();
        renderGames();
        loadPlatformFilter();
    }

    async function loadPlatformFilter() {
        const platforms = await getAvailablePlatforms();
        const select = document.getElementById('platform-filter');
        
        platforms.forEach(platform => {
            const option = document.createElement('option');
            option.value = platform;
            option.textContent = getPlatformName(platform);
            select.appendChild(option);
        });
    }

    function setupEventListeners() {
        const refreshBtn = document.getElementById('refresh-games');
        const filter = document.getElementById('platform-filter');

        if (refreshBtn) {
            refreshBtn.addEventListener('click', async () => {
                refreshBtn.disabled = true;
                refreshBtn.textContent = '⏳ Cargando...';
                
                if (selectedPlatform === 'all') {
                    await loadDetectedGames();
                } else {
                    await loadPlatformGames(selectedPlatform);
                }
                
                renderGames();
                refreshBtn.disabled = false;
                refreshBtn.textContent = '🔄 Refrescar';
            });
        }

        if (filter) {
            filter.addEventListener('change', async (e) => {
                selectedPlatform = e.target.value;
                
                if (selectedPlatform === 'all') {
                    await loadDetectedGames();
                } else {
                    await loadPlatformGames(selectedPlatform);
                }
                
                renderGames();
            });
        }
    }

    // ===== RENDERING =====

    function renderGames() {
        const grid = document.getElementById('games-grid');
        if (!grid) return;

        if (detectedGames.length === 0) {
            grid.innerHTML = `
                <div class="no-games">
                    <p>No se encontraron juegos instalados.</p>
                    <p>Asegúrate de tener Steam, Epic Games u otra plataforma instalada.</p>
                </div>
            `;
            return;
        }

        grid.innerHTML = '';
        
        detectedGames.forEach(game => {
            const card = createGameCard(game);
            grid.appendChild(card);
        });
    }

    function createGameCard(game) {
        const card = document.createElement('div');
        card.className = 'game-card';
        card.dataset.gameId = game.id;

        const platformIcon = getPlatformIcon(game.platform);
        const platformColor = getPlatformColor(game.platform);

        card.innerHTML = `
            <div class="game-card-header" style="background: ${platformColor}">
                <span class="platform-badge">${platformIcon} ${game.platform.toUpperCase()}</span>
            </div>
            <div class="game-card-body">
                <h3 class="game-title" title="${escapeHtml(game.name)}">
                    ${escapeHtml(game.name)}
                </h3>
                <div class="game-info">
                    <small>📁 ${truncatePath(game.install_dir || 'N/A')}</small>
                </div>
            </div>
            <div class="game-card-footer">
                <button class="btn btn-sm btn-primary launch-game" data-cmd="${escapeHtml(game.launch_cmd)}">
                    ▶️ Lanzar
                </button>
            </div>
        `;

        const launchBtn = card.querySelector('.launch-game');
        launchBtn.addEventListener('click', () => launchGame(game));

        return card;
    }

    // ===== GAME ACTIONS =====

    async function launchGame(game) {
        try {
            console.log('Launching game:', game.name);
            console.log('Command:', game.launch_cmd);
            
            // Usar la API de Sunshine para lanzar aplicaciones
            // Esto depende de cómo Sunshine maneje el lanzamiento de apps
            
            showNotification(`Lanzando ${game.name}...`, 'info');
            
            // Si el juego tiene un comando de lanzamiento, intentar ejecutarlo
            if (game.launch_cmd.startsWith('steam://')) {
                window.location.href = game.launch_cmd;
            } else {
                // Para otros juegos, puede requerir integración con la API de Sunshine
                console.warn('Launch method not implemented for this platform');
            }
            
        } catch (error) {
            console.error('Failed to launch game:', error);
            showNotification(`Error al lanzar ${game.name}`, 'error');
        }
    }

    // ===== UTILITIES =====

    function getPlatformName(platform) {
        const names = {
            'steam': 'Steam',
            'epic': 'Epic Games',
            'gog': 'GOG Galaxy',
            'xbox': 'Xbox Game Pass',
            'ea': 'EA Desktop',
            'ubisoft': 'Ubisoft Connect'
        };
        return names[platform] || platform;
    }

    function getPlatformIcon(platform) {
        const icons = {
            'steam': '🎮',
            'epic': '🏛️',
            'gog': '🌟',
            'xbox': '🎯',
            'ea': '⚡',
            'ubisoft': '🔷'
        };
        return icons[platform] || '🎮';
    }

    function getPlatformColor(platform) {
        const colors = {
            'steam': 'linear-gradient(135deg, #1b2838, #2a475e)',
            'epic': 'linear-gradient(135deg, #0078f2, #00a8ff)',
            'gog': 'linear-gradient(135deg, #8f2d56, #b94277)',
            'xbox': 'linear-gradient(135deg, #107c10, #00d800)',
            'ea': 'linear-gradient(135deg, #ff0000, #cc0000)',
            'ubisoft': 'linear-gradient(135deg, #0055b8, #0077cc)'
        };
        return colors[platform] || 'linear-gradient(135deg, #555, #777)';
    }

    function truncatePath(path, maxLength = 40) {
        if (!path || path.length <= maxLength) return path;
        return '...' + path.slice(-maxLength);
    }

    function escapeHtml(text) {
        const div = document.createElement('div');
        div.textContent = text;
        return div.innerHTML;
    }

    function showNotification(message, type = 'info') {
        console.log(`[${type.toUpperCase()}] ${message}`);
        
        // Crear notificación simple
        const notification = document.createElement('div');
        notification.className = `notification notification-${type}`;
        notification.textContent = message;
        
        document.body.appendChild(notification);
        
        setTimeout(() => notification.classList.add('show'), 100);
        setTimeout(() => {
            notification.classList.remove('show');
            setTimeout(() => notification.remove(), 300);
        }, 3000);
    }

    // ===== EXPORT =====

    window.GameDetector = {
        init: init,
        refresh: loadDetectedGames,
        getGames: () => detectedGames
    };

    // Auto-init cuando el DOM esté listo
    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', init);
    } else {
        init();
    }

})();