import { API_ENDPOINTS } from '../utils/constants.js';
import { formatError } from '../utils/helpers.js';

/**
 * 应用服务类
 */
export class AppService {
  /**
   * 获取应用列表
   * @returns {Promise<Array>} 应用列表
   */
  static async getApps() {
    try {
      const response = await fetch(API_ENDPOINTS.APPS);
      if (!response.ok) {
        throw new Error(`获取应用列表失败: ${response.status}`);
      }
      const data = await response.json();
      return data.apps || [];
    } catch (error) {
      console.error('获取应用列表失败:', error);
      throw new Error(formatError(error));
    }
  }

  /**
   * 保存应用
   * @param {Array} apps 应用列表
   * @param {Object} editApp 编辑的应用（可选）
   * @returns {Promise<boolean>} 是否保存成功
   */
  static async saveApps(apps, editApp = null) {
    try {
      const response = await fetch(API_ENDPOINTS.APPS, {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json'
        },
        body: JSON.stringify({
          apps,
          editApp
        })
      });
      
      if (!response.ok) {
        throw new Error(`保存应用失败: ${response.status}`);
      }
      
      return true;
    } catch (error) {
      console.error('保存应用失败:', error);
      throw new Error(formatError(error));
    }
  }

  /**
   * 删除应用
   * @param {number} index 应用索引
   * @returns {Promise<boolean>} 是否删除成功
   */
  static async deleteApp(index) {
    try {
      const response = await fetch(API_ENDPOINTS.APP_DELETE(index), {
        method: 'DELETE'
      });
      
      if (!response.ok) {
        throw new Error(`删除应用失败: ${response.status}`);
      }
      
      return true;
    } catch (error) {
      console.error('删除应用失败:', error);
      throw new Error(formatError(error));
    }
  }

  /**
   * 获取平台信息
   * @returns {Promise<string>} 平台信息
   */
  static async getPlatform() {
    try {
      const response = await fetch(API_ENDPOINTS.CONFIG);
      if (!response.ok) {
        throw new Error(`获取平台信息失败: ${response.status}`);
      }
      const data = await response.json();
      return data.platform || 'windows';
    } catch (error) {
      console.error('获取平台信息失败:', error);
      // 默认返回windows平台
      return 'windows';
    }
  }

  /**
   * 搜索应用
   * @param {Array} apps 应用列表
   * @param {string} query 搜索关键词
   * @returns {Array} 搜索结果
   */
  static searchApps(apps, query) {
    if (!query || !query.trim()) {
      return [...apps];
    }
    
    const searchTerm = query.toLowerCase().trim();
    return apps.filter(app => 
      app.name.toLowerCase().includes(searchTerm) || 
      (app.cmd && app.cmd.toLowerCase().includes(searchTerm))
    );
  }

  /**
   * 验证应用数据
   * @param {Object} app 应用对象
   * @returns {Object} 验证结果
   */
  static validateApp(app) {
    const errors = [];
    
    if (!app.name || !app.name.trim()) {
      errors.push('应用名称不能为空');
    }
    
    if (!app.cmd || !app.cmd.trim()) {
      errors.push('应用命令不能为空');
    }
    
    // 验证退出超时时间
    if (app['exit-timeout'] !== undefined && 
        (isNaN(app['exit-timeout']) || app['exit-timeout'] < 0)) {
      errors.push('退出超时时间必须是非负数');
    }
    
    return {
      isValid: errors.length === 0,
      errors
    };
  }

  /**
   * 格式化应用数据
   * @param {Object} app 原始应用数据
   * @returns {Object} 格式化后的应用数据
   */
  static formatAppData(app) {
    return {
      name: app.name?.trim() || '',
      output: app.output?.trim() || '',
      cmd: app.cmd?.trim() || '',
      'exclude-global-prep-cmd': Boolean(app['exclude-global-prep-cmd']),
      elevated: Boolean(app.elevated),
      'auto-detach': Boolean(app['auto-detach']),
      'wait-all': Boolean(app['wait-all']),
      'exit-timeout': parseInt(app['exit-timeout']) || 5,
      'prep-cmd': Array.isArray(app['prep-cmd']) ? app['prep-cmd'] : [],
      'menu-cmd': Array.isArray(app['menu-cmd']) ? app['menu-cmd'] : [],
      detached: Array.isArray(app.detached) ? app.detached : [],
      'image-path': app['image-path']?.trim() || '',
      'working-dir': app['working-dir']?.trim() || ''
    };
  }
} 