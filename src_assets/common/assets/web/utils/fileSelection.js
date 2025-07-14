/**
 * 文件选择工具模块
 * 提供跨平台的文件和目录选择功能
 */

/**
 * 文件选择器类
 */
export class FileSelector {
  constructor(options = {}) {
    this.platform = options.platform || 'linux';
    this.onSuccess = options.onSuccess || (() => {});
    this.onError = options.onError || (() => {});
    this.onInfo = options.onInfo || (() => {});
    this.currentField = null;
    this.selectionType = null;
  }

  /**
   * 选择文件
   */
  async selectFile(fieldName, fileInput, callback) {
    this.currentField = fieldName;
    this.selectionType = 'file';
    
    // 检查是否在 Electron 环境
    if (this.isElectronEnvironment()) {
      return await this.selectFileElectron(fieldName, callback);
    } else {
      // 浏览器环境
      return this.selectFileBrowser(fileInput, callback);
    }
  }

  /**
   * 选择目录
   */
  async selectDirectory(fieldName, dirInput, callback) {
    this.currentField = fieldName;
    this.selectionType = 'directory';
    
    // 检查是否在 Electron 环境
    if (this.isElectronEnvironment()) {
      return await this.selectDirectoryElectron(fieldName, callback);
    } else {
      // 浏览器环境
      return this.selectDirectoryBrowser(dirInput, callback);
    }
  }

  /**
   * 浏览器环境下选择文件
   */
  selectFileBrowser(fileInput, callback) {
    if (!fileInput) {
      this.onError('文件输入元素不存在');
      return;
    }

    fileInput.value = ''; // 清空之前的选择
    fileInput.click();

    const handleFileSelected = (event) => {
      const file = event.target.files[0];
      if (file && this.currentField) {
        try {
          let filePath = this.processFilePath(file);
          
          if (callback) {
            callback(this.currentField, filePath);
          }
          
          this.onSuccess(`文件选择成功: ${filePath}`);
          
          // 提示用户可能需要手动调整路径
          if (!this.isElectronEnvironment()) {
            this.onInfo('浏览器环境下无法获取完整路径，请检查并手动调整路径');
          }
        } catch (error) {
          console.error('文件选择处理失败:', error);
          this.onError('文件选择处理失败，请重试');
        }
      }
      
      // 重置状态
      this.resetState();
      // 移除事件监听器
      fileInput.removeEventListener('change', handleFileSelected);
    };

    fileInput.addEventListener('change', handleFileSelected);
  }

  /**
   * 浏览器环境下选择目录
   */
  selectDirectoryBrowser(dirInput, callback) {
    if (!dirInput) {
      this.onError('目录输入元素不存在');
      return;
    }

    dirInput.value = ''; // 清空之前的选择
    dirInput.click();

    const handleDirectorySelected = (event) => {
      const files = event.target.files;
      if (files.length > 0 && this.currentField) {
        try {
          let dirPath = this.processDirectoryPath(files[0]);
          
          if (callback) {
            callback(this.currentField, dirPath);
          }
          
          this.onSuccess(`目录选择成功: ${dirPath}`);
          
          // 提示用户可能需要手动调整路径
          if (!this.isElectronEnvironment()) {
            this.onInfo('浏览器环境下无法获取完整路径，请检查并手动调整路径');
          }
        } catch (error) {
          console.error('目录选择处理失败:', error);
          this.onError('目录选择处理失败，请重试');
        }
      }
      
      // 重置状态
      this.resetState();
      // 移除事件监听器
      dirInput.removeEventListener('change', handleDirectorySelected);
    };

    dirInput.addEventListener('change', handleDirectorySelected);
  }

  /**
   * 检查是否在 Electron 环境
   */
  isElectronEnvironment() {
    return typeof window !== 'undefined' && 
           window.process && 
           window.process.type === 'renderer';
  }

  /**
   * Electron 环境下选择文件
   */
  async selectFileElectron(fieldName, callback) {
    try {
      const { dialog } = window.require('electron').remote;
      const result = await dialog.showOpenDialog({
        properties: ['openFile'],
        filters: [
          { name: '可执行文件', extensions: ['exe', 'app', 'sh', 'bat', 'cmd'] },
          { name: '所有文件', extensions: ['*'] }
        ]
      });

      if (!result.canceled && result.filePaths.length > 0) {
        const filePath = result.filePaths[0];
        
        if (callback) {
          callback(fieldName, filePath);
        }
        
        this.onSuccess(`文件选择成功: ${filePath}`);
        return filePath;
      }
    } catch (error) {
      console.error('文件选择失败:', error);
      this.onError('文件选择失败，请手动输入路径');
    }
    
    this.resetState();
    return null;
  }

  /**
   * Electron 环境下选择目录
   */
  async selectDirectoryElectron(fieldName, callback) {
    try {
      const { dialog } = window.require('electron').remote;
      const result = await dialog.showOpenDialog({
        properties: ['openDirectory']
      });

      if (!result.canceled && result.filePaths.length > 0) {
        const dirPath = result.filePaths[0];
        
        if (callback) {
          callback(fieldName, dirPath);
        }
        
        this.onSuccess(`目录选择成功: ${dirPath}`);
        return dirPath;
      }
    } catch (error) {
      console.error('目录选择失败:', error);
      this.onError('目录选择失败，请手动输入路径');
    }
    
    this.resetState();
    return null;
  }

  /**
   * 处理文件路径
   */
  processFilePath(file) {
    // 在浏览器环境中，我们只能获取文件名，无法获取完整路径
    // 但我们可以尝试获取相对路径或使用 webkitRelativePath
    let filePath = file.name;
    
    // 如果有相对路径信息，使用它
    if (file.webkitRelativePath) {
      filePath = file.webkitRelativePath;
    }
    
    // 如果是开发环境，可以模拟完整路径
    if (this.isDevelopmentEnvironment()) {
      filePath = this.simulateFullPath(filePath);
    }
    
    return filePath;
  }

  /**
   * 处理目录路径
   */
  processDirectoryPath(firstFile) {
    let dirPath = '';
    
    if (firstFile.webkitRelativePath) {
      // 提取目录路径
      const pathParts = firstFile.webkitRelativePath.split('/');
      dirPath = pathParts.slice(0, -1).join('/');
    }
    
    // 如果是开发环境，可以模拟完整路径
    if (this.isDevelopmentEnvironment()) {
      dirPath = this.simulateFullPath(dirPath);
    }
    
    return dirPath;
  }

  /**
   * 检查是否是开发环境
   */
  isDevelopmentEnvironment() {
    return (typeof process !== 'undefined' && process.env.NODE_ENV === 'development') || 
           (typeof window !== 'undefined' && 
            (window.location.hostname === 'localhost' ||
             window.location.hostname === '127.0.0.1'));
  }

  /**
   * 模拟完整路径（仅用于开发环境）
   */
  simulateFullPath(relativePath) {
    // 根据平台模拟路径
    if (this.platform === 'windows') {
      return `C:\\Users\\User\\${relativePath.replace(/\//g, '\\')}`;
    } else {
      return `/home/user/${relativePath}`;
    }
  }

  /**
   * 重置状态
   */
  resetState() {
    this.currentField = null;
    this.selectionType = null;
  }

  /**
   * 检查文件选择支持
   */
  checkFileSelectionSupport() {
    // 检查基本的文件API支持
    if (typeof window === 'undefined' || !window.File || !window.FileReader || !window.FileList || !window.Blob) {
      console.warn('文件选择功能在当前环境中不受支持');
      return false;
    }
    
    return true;
  }

  /**
   * 检查目录选择支持
   */
  checkDirectorySelectionSupport(dirInput) {
    if (!dirInput || !('webkitdirectory' in dirInput)) {
      console.warn('目录选择功能在当前浏览器中不受支持');
      return false;
    }
    
    return true;
  }

  /**
   * 获取字段占位符文本
   */
  getPlaceholderText(fieldName) {
    const placeholders = {
      'cmd': this.platform === 'windows' ? 'C:\\Program Files\\App\\app.exe' : '/usr/bin/app',
      'working-dir': this.platform === 'windows' ? 'C:\\Program Files\\App' : '/usr/bin'
    };
    return placeholders[fieldName] || '';
  }

  /**
   * 获取按钮标题文本
   */
  getButtonTitle(type) {
    const titles = {
      'file': '选择文件',
      'directory': '选择目录'
    };
    return titles[type] || '选择';
  }

  /**
   * 清理文件输入
   */
  cleanupFileInputs(fileInput, dirInput) {
    if (fileInput) {
      fileInput.value = '';
    }
    if (dirInput) {
      dirInput.value = '';
    }
  }
}

/**
 * 创建文件选择器实例的工厂函数
 */
export function createFileSelector(options = {}) {
  return new FileSelector(options);
}

/**
 * 简化的文件选择函数
 */
export async function selectFile(options = {}) {
  const selector = createFileSelector(options);
  return await selector.selectFile(
    options.fieldName,
    options.fileInput,
    options.callback
  );
}

/**
 * 简化的目录选择函数
 */
export async function selectDirectory(options = {}) {
  const selector = createFileSelector(options);
  return await selector.selectDirectory(
    options.fieldName,
    options.dirInput,
    options.callback
  );
}

/**
 * 检查环境支持
 */
export function checkEnvironmentSupport() {
  const selector = createFileSelector();
  return {
    fileSelection: selector.checkFileSelectionSupport(),
    directorySelection: selector.checkDirectorySelectionSupport(),
    isElectron: selector.isElectronEnvironment(),
    isDevelopment: selector.isDevelopmentEnvironment()
  };
}

export default FileSelector; 