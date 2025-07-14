/**
 * 图片工具函数
 * 用于处理应用图片URL的标准化逻辑
 */
/**
 * 获取图片预览URL
 * @param {string} imagePath 图片路径
 * @returns {string} 预览URL
 */
export function getImagePreviewUrl(imagePath = 'box.png') {
  if (imagePath === 'desktop') {
    return '/boxart/desktop.png'
  }
  // 如果路径不包含分隔符,说明是boxart资源ID
  if (!/[/\\]/.test(imagePath)) {
    return `/boxart/${encodeURIComponent(imagePath)}`
  }

  return isLocalImagePath(imagePath) ? `file://${imagePath}` : imagePath
}

/**
 * 检查图片路径是否为本地文件路径
 * @param {string} imagePath 图片路径
 * @returns {boolean} 是否为本地文件路径
 */
export function isLocalImagePath(imagePath) {
  if (!imagePath) {
    return false
  }

  // 如果是网络URL或blob/data URL，不是本地路径
  if (
    imagePath.startsWith('http://') ||
    imagePath.startsWith('https://') ||
    imagePath.startsWith('blob:') ||
    imagePath.startsWith('data:')
  ) {
    return false
  }

  return true
}
