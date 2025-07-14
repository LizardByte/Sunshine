import { APP_CONSTANTS } from './constants.js';

/**
 * 统一错误处理类
 */
export class ErrorHandler {
  /**
   * 处理网络错误
   * @param {Error} error 错误对象
   * @param {string} context 错误上下文
   * @returns {string} 用户友好的错误信息
   */
  static handleNetworkError(error, context = '操作') {
    console.error(`${context}失败:`, error);
    
    if (error.name === 'TypeError' && error.message.includes('Failed to fetch')) {
      return `网络连接失败，请检查网络连接后重试`;
    }
    
    if (error.message.includes('404')) {
      return `${context}失败：请求的资源不存在`;
    }
    
    if (error.message.includes('500')) {
      return `${context}失败：服务器内部错误`;
    }
    
    if (error.message.includes('403')) {
      return `${context}失败：权限不足`;
    }
    
    return `${context}失败：${error.message || '未知错误'}`;
  }

  /**
   * 处理验证错误
   * @param {Array} errors 错误数组
   * @returns {string} 格式化的错误信息
   */
  static handleValidationErrors(errors) {
    if (!Array.isArray(errors) || errors.length === 0) {
      return '验证失败';
    }
    
    return errors.join('；');
  }

  /**
   * 处理应用操作错误
   * @param {Error} error 错误对象
   * @param {string} operation 操作类型
   * @param {string} appName 应用名称
   * @returns {string} 格式化的错误信息
   */
  static handleAppError(error, operation, appName = '') {
    const appContext = appName ? `"${appName}"` : '';
    
    switch(operation) {
      case 'save':
        return this.handleNetworkError(error, `保存应用${appContext}`);
      case 'delete':
        return this.handleNetworkError(error, `删除应用${appContext}`);
      case 'load':
        return this.handleNetworkError(error, `加载应用${appContext}`);
      default:
        return this.handleNetworkError(error, `操作应用${appContext}`);
    }
  }

  /**
   * 创建错误弹窗
   * @param {string} message 错误信息
   * @param {string} title 标题
   */
  static showErrorDialog(message, title = '错误') {
    // 如果需要更复杂的错误弹窗，可以在这里实现
    // 目前使用简单的 alert
    alert(`${title}\n\n${message}`);
  }

  /**
   * 创建确认弹窗
   * @param {string} message 确认信息
   * @param {string} title 标题
   * @returns {boolean} 用户是否确认
   */
  static showConfirmDialog(message, title = '确认') {
    return confirm(`${title}\n\n${message}`);
  }

  /**
   * 记录错误日志
   * @param {Error} error 错误对象
   * @param {string} context 错误上下文
   * @param {Object} metadata 额外的元数据
   */
  static logError(error, context = '', metadata = {}) {
    const errorInfo = {
      message: error.message,
      stack: error.stack,
      context,
      timestamp: new Date().toISOString(),
      ...metadata
    };
    
    console.error('应用错误:', errorInfo);
    
    // 如果需要发送到日志服务，可以在这里实现
    // this.sendToLogService(errorInfo);
  }

  /**
   * 处理异步操作错误
   * @param {Promise} promise Promise对象
   * @param {string} context 错误上下文
   * @returns {Promise} 包装后的Promise
   */
  static async handleAsyncError(promise, context = '') {
    try {
      return await promise;
    } catch (error) {
      this.logError(error, context);
      throw error;
    }
  }
} 