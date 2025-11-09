/**
 * @file game-detector.css
 * @brief Estilos para el módulo de detección de juegos
 * 
 * Ubicación: src_assets/common/assets/web/public/assets/css/game-detector.css
 */

/* ===== CONTENEDOR PRINCIPAL ===== */

.game-detector-section {
    margin: 2rem 0;
    padding: 1.5rem;
    background: linear-gradient(135deg, #1a1a2e 0%, #16213e 100%);
    border-radius: 12px;
    box-shadow: 0 4px 20px rgba(0, 0, 0, 0.3);
}

/* ===== HEADER ===== */

.game-detector-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    margin-bottom: 1.5rem;
    padding-bottom: 1rem;
    border-bottom: 2px solid rgba(255, 255, 255, 0.1);
}

.game-detector-header h2 {
    margin: 0;
    font-size: 1.5rem;
    color: #fff;
    font-weight: 600;
}

.game-detector-controls {
    display: flex;
    gap: 1rem;
    align-items: center;
}

#platform-filter {
    padding: 0.5rem 1rem;
    background: rgba(255, 255, 255, 0.1);
    border: 1px solid rgba(255, 255, 255, 0.2);
    border-radius: 6px;
    color: #fff;
    font-size: 0.9rem;
    cursor: pointer;
    transition: all 0.3s ease;
}

#platform-filter:hover {
    background: rgba(255, 255, 255, 0.15);
    border-color: rgba(255, 255, 255, 0.3);
}

#platform-filter:focus {
    outline: none;
    border-color: #4a9eff;
    box-shadow: 0 0 0 3px rgba(74, 158, 255, 0.2);
}

#platform-filter option {
    background: #1a1a2e;
    color: #fff;
}

/* ===== GRID DE JUEGOS ===== */

.games-grid {
    display: grid;
    grid-template-columns: repeat(auto-fill, minmax(280px, 1fr));
    gap: 1.25rem;
    animation: fadeIn 0.5s ease-in;
}

@keyframes fadeIn {
    from {
        opacity: 0;
        transform: translateY(10px);
    }
    to {
        opacity: 1;
        transform: translateY(0);
    }
}

/* ===== TARJETAS DE JUEGO ===== */

.game-card {
    background: linear-gradient(135deg, #2d3748 0%, #1a202c 100%);
    border-radius: 10px;
    overflow: hidden;
    box-shadow: 0 4px 15px rgba(0, 0, 0, 0.2);
    transition: all 0.3s cubic-bezier(0.4, 0, 0.2, 1);
    cursor: pointer;
}

.game-card:hover {
    transform: translateY(-5px);
    box-shadow: 0 8px 25px rgba(0, 0, 0, 0.4);
}

.game-card-header {
    padding: 0.75rem 1rem;
    display: flex;
    justify-content: space-between;
    align-items: center;
}

.platform-badge {
    display: inline-block;
    padding: 0.25rem 0.75rem;
    background: rgba(0, 0, 0, 0.3);
    border-radius: 20px;
    font-size: 0.75rem;
    font-weight: 600;
    color: #fff;
    text-transform: uppercase;
    letter-spacing: 0.5px;
}

.game-card-body {
    padding: 1rem;
    min-height: 100px;
}

.game-title {
    margin: 0 0 0.5rem 0;
    font-size: 1.1rem;
    font-weight: 600;
    color: #fff;
    line-height: 1.3;
    overflow: hidden;
    text-overflow: ellipsis;
    display: -webkit-box;
    -webkit-line-clamp: 2;
    -webkit-box-orient: vertical;
}

.game-info {
    margin-top: 0.5rem;
    color: rgba(255, 255, 255, 0.6);
    font-size: 0.85rem;
}

.game-info small {
    display: block;
    margin-top: 0.25rem;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
}

.game-card-footer {
    padding: 1rem;
    background: rgba(0, 0, 0, 0.2);
    border-top: 1px solid rgba(255, 255, 255, 0.1);
}

/* ===== BOTONES ===== */

.launch-game {
    width: 100%;
    padding: 0.5rem 1rem;
    background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
    color: #fff;
    border: none;
    border-radius: 6px;
    font-size: 0.9rem;
    font-weight: 600;
    cursor: pointer;
    transition: all 0.2s ease;
}

.launch-game:hover {
    transform: translateY(-2px);
    box-shadow: 0 4px 12px rgba(102, 126, 234, 0.4);
}

.launch-game:active {
    transform: translateY(0);
}

#refresh-games {
    padding: 0.5rem 1rem;
    background: rgba(255, 255, 255, 0.1);
    color: #fff;
    border: 1px solid rgba(255, 255, 255, 0.2);
    border-radius: 6px;
    cursor: pointer;
    transition: all 0.2s ease;
}

#refresh-games:hover {
    background: rgba(255, 255, 255, 0.15);
    border-color: rgba(255, 255, 255, 0.3);
}

#refresh-games:disabled {
    opacity: 0.5;
    cursor: not-allowed;
}

/* ===== MENSAJE SIN JUEGOS ===== */

.no-games {
    text-align: center;
    padding: 3rem 2rem;
    color: rgba(255, 255, 255, 0.6);
    grid-column: 1 / -1;
}

.no-games p {
    margin: 0.5rem 0;
    font-size: 1rem;
}

.no-games p:first-child {
    font-size: 1.2rem;
    font-weight: 600;
    color: rgba(255, 255, 255, 0.8);
}

/* ===== NOTIFICACIONES ===== */

.notification {
    position: fixed;
    bottom: 2rem;
    right: 2rem;
    padding: 1rem 1.5rem;
    background: #2d3748;
    color: #fff;
    border-radius: 8px;
    box-shadow: 0 4px 20px rgba(0, 0, 0, 0.3);
    opacity: 0;
    transform: translateY(20px);
    transition: all 0.3s cubic-bezier(0.4, 0, 0.2, 1);
    z-index: 10000;
    max-width: 400px;
}

.notification.show {
    opacity: 1;
    transform: translateY(0);
}

.notification-success {
    background: linear-gradient(135deg, #11998e 0%, #38ef7d 100%);
}

.notification-error {
    background: linear-gradient(135deg, #eb3349 0%, #f45c43 100%);
}

.notification-info {
    background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
}

/* ===== RESPONSIVE ===== */

@media (max-width: 768px) {
    .game-detector-header {
        flex-direction: column;
        align-items: flex-start;
        gap: 1rem;
    }
    
    .game-detector-controls {
        width: 100%;
        flex-wrap: wrap;
    }
    
    #platform-filter {
        flex: 1;
        min-width: 150px;
    }
    
    .games-grid {
        grid-template-columns: repeat(auto-fill, minmax(250px, 1fr));
        gap: 1rem;
    }
}

@media (max-width: 480px) {
    .games-grid {
        grid-template-columns: 1fr;
    }
    
    .game-detector-section {
        padding: 1rem;
    }
    
    .game-detector-header h2 {
        font-size: 1.25rem;
    }
}

/* ===== ANIMACIONES ===== */

.game-card {
    animation: slideIn 0.4s ease-out backwards;
}

@keyframes slideIn {
    from {
        opacity: 0;
        transform: translateX(-20px);
    }
    to {
        opacity: 1;
        transform: translateX(0);
    }
}

.game-card:nth-child(1) { animation-delay: 0.05s; }
.game-card:nth-child(2) { animation-delay: 0.1s; }
.game-card:nth-child(3) { animation-delay: 0.15s; }
.game-card:nth-child(4) { animation-delay: 0.2s; }
.game-card:nth-child(5) { animation-delay: 0.25s; }
.game-card:nth-child(6) { animation-delay: 0.3s; }