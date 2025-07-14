// 应用管理相关常量
export const APP_CONSTANTS = {
  // 消息类型
  MESSAGE_TYPES: {
    SUCCESS: 'success',
    ERROR: 'error',
    WARNING: 'warning',
    INFO: 'info'
  },
  
  // 消息图标映射
  MESSAGE_ICONS: {
    success: 'fa-check-circle',
    error: 'fa-exclamation-circle',
    warning: 'fa-exclamation-triangle',
    info: 'fa-info-circle'
  },
  
  // 默认应用配置
  DEFAULT_APP: {
    name: "",
    output: "",
    cmd: "",
    index: -1,
    "exclude-global-prep-cmd": false,
    elevated: false,
    "auto-detach": true,
    "wait-all": true,
    "exit-timeout": 5,
    "prep-cmd": [],
    "menu-cmd": [],
    detached: [],
    "image-path": "",
    "working-dir": ""
  },
  
  // 支持的平台
  PLATFORMS: {
    WINDOWS: 'windows',
    LINUX: 'linux',
    MACOS: 'macos'
  },
  
  // 消息自动隐藏时间
  MESSAGE_AUTO_HIDE_TIME: 3000,
  
  // 拖拽动画时间
  DRAG_ANIMATION_TIME: 300,
  
  // 复制成功动画时间
  COPY_SUCCESS_ANIMATION_TIME: 400,
  
  // 搜索防抖时间
  SEARCH_DEBOUNCE_TIME: 300,
  
  // 文本截断长度
  TEXT_TRUNCATE_LENGTH: 50
};

// 环境变量配置
export const ENV_VARS_CONFIG = {
  'SUNSHINE_APP_ID': 'apps.env_app_id',
  'SUNSHINE_APP_NAME': 'apps.env_app_name',
  'SUNSHINE_CLIENT_NAME': 'apps.env_client_name',
  'SUNSHINE_CLIENT_WIDTH': 'apps.env_client_width',
  'SUNSHINE_CLIENT_HEIGHT': 'apps.env_client_height',
  'SUNSHINE_CLIENT_FPS': 'apps.env_client_fps',
  'SUNSHINE_CLIENT_HDR': 'apps.env_client_hdr',
  'SUNSHINE_CLIENT_GCMAP': 'apps.env_client_gcmap',
  'SUNSHINE_CLIENT_HOST_AUDIO': 'apps.env_client_host_audio',
  'SUNSHINE_CLIENT_ENABLE_SOPS': 'apps.env_client_enable_sops',
  'SUNSHINE_CLIENT_AUDIO_CONFIGURATION': 'apps.env_client_audio_config'
};

// API端点
export const API_ENDPOINTS = {
  APPS: '/api/apps',
  CONFIG: '/api/config',
  APP_DELETE: (index) => `/api/apps/${index}`
}; 