<template>
  <div class="app-card" :class="{ 'app-card-dragging': isDragging }">
    <div class="app-card-inner">
      <!-- 应用图标 -->
      <div class="app-icon-container">
        <img 
          v-if="app['image-path']" 
          :src="getImageUrl()" 
          :alt="app.name"
          class="app-icon"
          @error="handleImageError"
        >
        <div v-else class="app-icon-placeholder">
          <i class="fas fa-desktop"></i>
        </div>
      </div>
      
      <!-- 应用信息 -->
      <div class="app-info" :title="app.cmd" @click="copyToClipboard(app.cmd, app.name, $event)">
        <h3 class="app-name">{{ app.name }}</h3>
        <p class="app-command" v-if="app.cmd">
          <i class="fas fa-terminal me-1"></i>
          {{ truncateText(app.cmd, 50) }}
        </p>
        <div class="app-tags">
          <span v-if="app['exclude-global-prep-cmd'] && app['exclude-global-prep-cmd'] !== 'false'" class="app-tag tag-exclude-global-prep-cmd">
            <i class="fas fa-ellipsis-h me-1"></i>全局预处理命令
          </span>
          <span v-if="app['menu-cmd'] && app['menu-cmd'].length > 0" class="app-tag tag-menu">
            <span class="badge rounded-pill bg-secondary me-1">{{ app['menu-cmd'].length }}</span>菜单命令
          </span>
          <span v-if="app.elevated && app.elevated !== 'false'" class="app-tag tag-elevated">
            <i class="fas fa-shield-alt me-1"></i>管理员
          </span>
          <span v-if="app['auto-detach'] && app['auto-detach'] !== 'false'" class="app-tag tag-detach">
            <i class="fas fa-unlink me-1"></i>分离
          </span>
        </div>
      </div>
      
      <!-- 操作按钮 -->
      <div class="app-actions">
        <button 
          class="btn btn-edit" 
          @click="$emit('edit')"
          :title="$t('apps.edit')"
        >
          <i class="fas fa-edit"></i>
        </button>
        <button 
          class="btn btn-delete" 
          @click="$emit('delete')"
          :title="$t('apps.delete')"
        >
          <i class="fas fa-trash"></i>
        </button>
      </div>
      
      <!-- 拖拽手柄 -->
      <div v-if="draggable" class="drag-handle">
        <i class="fas fa-grip-vertical"></i>
      </div>
      
      <!-- 搜索状态指示 -->
      <div v-if="isSearchResult" class="search-indicator">
        <i class="fas fa-search"></i>
      </div>
    </div>
  </div>
</template>

<script>
import { getImagePreviewUrl } from '../utils/imageUtils.js';

export default {
  name: 'AppCard',
  props: {
    app: {
      type: Object,
      required: true
    },
    draggable: {
      type: Boolean,
      default: true
    },
    isSearchResult: {
      type: Boolean,
      default: false
    },
    isDragging: {
      type: Boolean,
      default: false
    }
  },
  emits: ['edit', 'delete', 'copy-success', 'copy-error'],
  methods: {
    /**
     * 处理图像错误
     */
    handleImageError(event) {
      const element = event.target;
      element.style.display = 'none';
      if (element.nextElementSibling) {
        element.nextElementSibling.style.display = 'flex';
      }
    },
    
    /**
     * 获取图片URL
     */
    getImageUrl() {
      return getImagePreviewUrl(this.app['image-path']);
    },
    
    /**
     * 截断文本
     */
    truncateText(text, length) {
      if (!text) return '';
      if (text.length <= length) return text;
      return text.substring(0, length) + '...';
    },
    
    /**
     * 复制到剪贴板
     */
    async copyToClipboard(text, appName, event) {
      if (!text) {
        this.$emit('copy-error', '没有可复制的命令');
        return;
      }
      
      const targetElement = event.currentTarget;
      
      try {
        // 使用现代的 Clipboard API
        if (navigator.clipboard && window.isSecureContext) {
          await navigator.clipboard.writeText(text);
          this.showCopySuccess(targetElement, appName);
        } else {
          // 回退方案：使用传统的 execCommand
          const textArea = document.createElement('textarea');
          textArea.value = text;
          textArea.style.position = 'fixed';
          textArea.style.left = '-999999px';
          textArea.style.top = '-999999px';
          document.body.appendChild(textArea);
          textArea.focus();
          textArea.select();
          
          try {
            document.execCommand('copy');
            this.showCopySuccess(targetElement, appName);
          } catch (err) {
            console.error('复制失败:', err);
            this.$emit('copy-error', '复制失败，请手动复制');
          } finally {
            document.body.removeChild(textArea);
          }
        }
      } catch (err) {
        console.error('复制到剪贴板失败:', err);
        this.$emit('copy-error', '复制失败，请检查浏览器权限');
      }
    },
    
    /**
     * 显示复制成功动画和消息
     */
    showCopySuccess(element, appName) {
      // 添加动画类
      element.classList.add('copy-success');
      
      // 发出成功事件
      this.$emit('copy-success', `📋 已复制 "${appName}" 的命令`);
      
      // 400ms后移除动画类
      setTimeout(() => {
        element.classList.remove('copy-success');
      }, 400);
    },
  }
}
</script> 