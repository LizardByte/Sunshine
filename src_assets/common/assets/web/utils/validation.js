/**
 * 表单验证工具模块
 * 提供应用表单的各种验证规则和方法
 */

/**
 * 验证规则对象
 */
export const validationRules = {
  // 应用名称验证
  appName: {
    required: true,
    minLength: 1,
    maxLength: 100,
    pattern: /^[^<>:"\\|?*\x00-\x1F]+$/,
    message: '应用名称不能为空，且不能包含特殊字符'
  },
  
  // 命令验证
  command: {
    required: true,
    minLength: 1,
    maxLength: 1000,
    message: '命令不能为空'
  },
  
  // 工作目录验证
  workingDir: {
    required: false,
    maxLength: 500,
    message: '工作目录路径过长'
  },
  
  // 输出名称验证
  outputName: {
    required: false,
    maxLength: 100,
    pattern: /^[a-zA-Z0-9_\-\.]*$/,
    message: '输出名称只能包含字母、数字、下划线、连字符和点'
  },
  
  // 超时时间验证
  timeout: {
    required: false,
    min: 0,
    max: 3600,
    message: '超时时间必须在0-3600秒之间'
  },
  
  // 图片路径验证
  imagePath: {
    required: false,
    maxLength: 500,
    allowedTypes: ['png', 'jpg', 'jpeg', 'gif', 'bmp', 'webp'],
    message: '图片路径无效或格式不支持'
  }
};

/**
 * 验证单个字段
 * @param {string} fieldName 字段名称
 * @param {any} value 字段值
 * @param {Object} customRules 自定义验证规则
 * @returns {Object} 验证结果 {isValid: boolean, message: string}
 */
export function validateField(fieldName, value, customRules = {}) {
  const rules = { ...validationRules[fieldName], ...customRules };
  
  if (!rules) {
    return { isValid: true, message: '' };
  }
  
  // 必填验证
  if (rules.required && (!value || value.toString().trim() === '')) {
    return { isValid: false, message: rules.message || '此字段为必填项' };
  }
  
  // 如果字段为空且不是必填，则跳过其他验证
  if (!value || value.toString().trim() === '') {
    return { isValid: true, message: '' };
  }
  
  const strValue = value.toString().trim();
  
  // 长度验证
  if (rules.minLength && strValue.length < rules.minLength) {
    return { isValid: false, message: `最少需要${rules.minLength}个字符` };
  }
  
  if (rules.maxLength && strValue.length > rules.maxLength) {
    return { isValid: false, message: `最多允许${rules.maxLength}个字符` };
  }
  
  // 数值验证
  if (rules.min !== undefined || rules.max !== undefined) {
    const numValue = Number(value);
    if (isNaN(numValue)) {
      return { isValid: false, message: '请输入有效的数字' };
    }
    
    if (rules.min !== undefined && numValue < rules.min) {
      return { isValid: false, message: `最小值为${rules.min}` };
    }
    
    if (rules.max !== undefined && numValue > rules.max) {
      return { isValid: false, message: `最大值为${rules.max}` };
    }
  }
  
  // 正则表达式验证
  if (rules.pattern && !rules.pattern.test(strValue)) {
    return { isValid: false, message: rules.message || '格式不正确' };
  }
  
  // 文件类型验证
  if (rules.allowedTypes && fieldName === 'imagePath' && strValue !== 'desktop') {
    const extension = strValue
      .slice(Math.max(0, strValue.lastIndexOf('.') + 1)) // 获取最后一个点号后的部分
      .split(/[?#]/)[0]  // 去除可能存在的查询参数和哈希
      .toLowerCase();
    if (extension && !rules.allowedTypes.includes(extension)) {
      return { 
        isValid: false, 
        message: `只支持以下格式：${rules.allowedTypes.join(', ')}` 
      };
    }
  }
  
  return { isValid: true, message: '' };
}

/**
 * 验证应用表单
 * @param {Object} formData 表单数据
 * @returns {Object} 验证结果
 */
export function validateAppForm(formData) {
  const results = {};
  const errors = [];
  
  // 验证应用名称
  const nameResult = validateField('appName', formData.name);
  results.name = nameResult;
  if (!nameResult.isValid) errors.push(`应用名称: ${nameResult.message}`);
  
  // 验证命令
  const cmdResult = validateField('command', formData.cmd);
  results.cmd = cmdResult;
  if (!cmdResult.isValid) errors.push(`命令: ${cmdResult.message}`);
  
  // 验证工作目录
  const workingDirResult = validateField('workingDir', formData['working-dir']);
  results['working-dir'] = workingDirResult;
  if (!workingDirResult.isValid) errors.push(`工作目录: ${workingDirResult.message}`);
  
  // 验证输出名称
  const outputResult = validateField('outputName', formData.output);
  results.output = outputResult;
  if (!outputResult.isValid) errors.push(`输出名称: ${outputResult.message}`);
  
  // 验证超时时间
  const timeoutResult = validateField('timeout', formData['exit-timeout']);
  results['exit-timeout'] = timeoutResult;
  if (!timeoutResult.isValid) errors.push(`超时时间: ${timeoutResult.message}`);
  
  // 验证图片路径
  const imageResult = validateField('imagePath', formData['image-path']);
  results['image-path'] = imageResult;
  if (!imageResult.isValid) errors.push(`图片路径: ${imageResult.message}`);
  
  // 验证准备命令
  if (formData['prep-cmd'] && Array.isArray(formData['prep-cmd'])) {
    formData['prep-cmd'].forEach((cmd, index) => {
      if (!cmd.do || cmd.do.trim() === '') {
        errors.push(`准备命令 ${index + 1}: 执行命令不能为空`);
      }
    });
  }
  
  // 验证菜单命令
  if (formData['menu-cmd'] && Array.isArray(formData['menu-cmd'])) {
    formData['menu-cmd'].forEach((cmd, index) => {
      if (!cmd.name || cmd.name.trim() === '') {
        errors.push(`菜单命令 ${index + 1}: 显示名称不能为空`);
      }
      if (!cmd.cmd || cmd.cmd.trim() === '') {
        errors.push(`菜单命令 ${index + 1}: 命令不能为空`);
      }
    });
  }
  
  // 验证独立命令
  if (formData.detached && Array.isArray(formData.detached)) {
    formData.detached.forEach((cmd, index) => {
      if (cmd && cmd.trim() === '') {
        errors.push(`独立命令 ${index + 1}: 命令不能为空`);
      }
    });
  }
  
  return {
    isValid: errors.length === 0,
    errors,
    fieldResults: results
  };
}

/**
 * 验证文件
 * @param {File} file 文件对象
 * @param {Object} options 验证选项
 * @returns {Object} 验证结果
 */
export function validateFile(file, options = {}) {
  const {
    allowedTypes = ['image/png', 'image/jpg', 'image/jpeg', 'image/gif', 'image/bmp', 'image/webp'],
    maxSize = 10 * 1024 * 1024, // 10MB
    minSize = 0
  } = options;
  
  if (!file) {
    return { isValid: false, message: '请选择文件' };
  }
  
  // 检查文件类型
  if (!allowedTypes.includes(file.type)) {
    return { 
      isValid: false, 
      message: `不支持的文件类型。支持的格式：${allowedTypes.join(', ')}` 
    };
  }
  
  // 检查文件大小
  if (file.size > maxSize) {
    return { 
      isValid: false, 
      message: `文件大小不能超过 ${(maxSize / (1024 * 1024)).toFixed(1)}MB` 
    };
  }
  
  if (file.size < minSize) {
    return { 
      isValid: false, 
      message: `文件大小不能小于 ${(minSize / 1024).toFixed(1)}KB` 
    };
  }
  
  return { isValid: true, message: '' };
}

/**
 * 实时验证混合器
 * @param {Object} formData 表单数据
 * @param {Array} watchFields 需要监听的字段
 * @returns {Object} 验证状态
 */
export function createFormValidator(formData, watchFields = []) {
  const validationStates = {};
  
  // 初始化验证状态
  watchFields.forEach(field => {
    validationStates[field] = { isValid: true, message: '' };
  });
  
  const validator = {
    // 验证单个字段
    validateField(fieldName, value) {
      const result = validateField(fieldName, value);
      validationStates[fieldName] = result;
      return result;
    },
    
    // 验证整个表单
    validateForm() {
      return validateAppForm(formData);
    },
    
    // 获取字段验证状态
    getFieldState(fieldName) {
      return validationStates[fieldName] || { isValid: true, message: '' };
    },
    
    // 获取所有验证状态
    getAllStates() {
      return { ...validationStates };
    },
    
    // 重置验证状态
    resetValidation() {
      Object.keys(validationStates).forEach(key => {
        validationStates[key] = { isValid: true, message: '' };
      });
    }
  };
  
  return validator;
}

/**
 * 创建防抖验证器
 * @param {Function} validationFn 验证函数
 * @param {number} delay 防抖延迟时间
 * @returns {Function} 防抖后的验证函数
 */
export function createDebouncedValidator(validationFn, delay = 300) {
  let timeoutId;
  
  return function(...args) {
    clearTimeout(timeoutId);
    return new Promise((resolve) => {
      timeoutId = setTimeout(() => {
        resolve(validationFn(...args));
      }, delay);
    });
  };
}

export default {
  validationRules,
  validateField,
  validateAppForm,
  validateFile,
  createFormValidator,
  createDebouncedValidator
}; 