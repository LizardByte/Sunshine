# 工具模块文档

## 文件选择模块 (fileSelection.js)

提供跨平台的文件和目录选择功能，支持 Electron 和浏览器环境。

### 快速开始

```javascript
import { createFileSelector } from './utils/fileSelection.js';

// 创建文件选择器实例
const fileSelector = createFileSelector({
  platform: 'windows', // 'windows', 'linux', 'macos'
  onSuccess: (message) => console.log(message),
  onError: (error) => console.error(error),
  onInfo: (info) => console.info(info)
});

// 选择文件
fileSelector.selectFile('cmd', fileInputRef, (fieldName, filePath) => {
  console.log(`选择的文件: ${filePath}`);
});

// 选择目录
fileSelector.selectDirectory('working-dir', dirInputRef, (fieldName, dirPath) => {
  console.log(`选择的目录: ${dirPath}`);
});
```

### 简化使用

```javascript
import { selectFile, selectDirectory } from './utils/fileSelection.js';

// 直接选择文件
selectFile({
  fieldName: 'cmd',
  fileInput: fileInputRef,
  platform: 'windows',
  callback: (fieldName, filePath) => {
    // 处理选择的文件
  }
});

// 直接选择目录
selectDirectory({
  fieldName: 'working-dir',
  dirInput: dirInputRef,
  platform: 'windows',
  callback: (fieldName, dirPath) => {
    // 处理选择的目录
  }
});
```

### 环境检查

```javascript
import { checkEnvironmentSupport } from './utils/fileSelection.js';

const support = checkEnvironmentSupport();
console.log('文件选择支持:', support.fileSelection);
console.log('目录选择支持:', support.directorySelection);
console.log('Electron环境:', support.isElectron);
console.log('开发环境:', support.isDevelopment);
```

### 在 Vue 组件中使用

```javascript
import { createFileSelector } from '../utils/fileSelection.js';

export default {
  data() {
    return {
      fileSelector: null
    };
  },
  
  mounted() {
    this.fileSelector = createFileSelector({
      platform: this.platform,
      onSuccess: this.showSuccess,
      onError: this.showError,
      onInfo: this.showInfo
    });
  },
  
  methods: {
    selectFile(fieldName) {
      this.fileSelector.selectFile(
        fieldName,
        this.$refs.fileInput,
        this.onFileSelected
      );
    },
    
    onFileSelected(fieldName, filePath) {
      this.formData[fieldName] = filePath;
      this.validateField(fieldName);
    }
  }
};
```

### 支持的平台

- **Electron**: 完整的原生文件/目录选择对话框
- **浏览器**: HTML5 文件API，有安全限制
- **开发环境**: 模拟完整路径，便于测试

### 注意事项

- 浏览器环境下无法获取完整系统路径
- 开发环境会自动模拟完整路径
- Electron环境提供最佳的用户体验

## 表单验证模块 (validation.js)

提供表单字段验证功能。

### 使用方法

```javascript
import { validateField, validateAppForm } from './utils/validation.js';

// 验证单个字段
const result = validateField('appName', 'MyApp');
console.log(result.isValid); // true/false
console.log(result.message); // 错误消息

// 验证整个表单
const formResult = validateAppForm(formData);
console.log(formResult.isValid); // true/false
console.log(formResult.errors); // 错误列表
```

## Steam API 模块 (steamApi.js)

提供Steam Store API集成功能。

### 使用方法

```javascript
import { searchSteamApps, findAppCover } from './utils/steamApi.js';

// 搜索Steam应用
const apps = await searchSteamApps('Half-Life');

// 查找应用封面
const cover = await findAppCover('Half-Life 2');
``` 