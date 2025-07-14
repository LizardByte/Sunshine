/**
 * Steam API工具模块
 * 提供Steam应用搜索和封面获取功能
 */

// Steam应用列表缓存
let steamAppsCache = [];

/**
 * 加载Steam应用列表
 * @returns {Promise<Array>} Steam应用列表
 */
export async function loadSteamApps() {
  if (steamAppsCache.length > 0) {
    return steamAppsCache;
  }

  try {
    const response = await fetch('/steam-api/ISteamApps/GetAppList/v2/');
    const data = await response.json();
    steamAppsCache = data.applist.apps;
    return steamAppsCache;
  } catch (error) {
    console.warn('无法加载Steam应用列表:', error);
    return [];
  }
}

/**
 * 搜索Steam应用
 * @param {string} searchName 搜索名称
 * @param {number} maxResults 最大结果数量
 * @returns {Array} 匹配的Steam应用列表
 */
export function searchSteamApps(searchName, maxResults = 20) {
  if (!searchName || steamAppsCache.length === 0) {
    return [];
  }

  const lowerSearchName = searchName.toLowerCase();
  
  return steamAppsCache
    .filter(app => app.name.toLowerCase().includes(lowerSearchName))
    .sort((a, b) => {
      // 优先显示完全匹配的结果
      const aExact = a.name.toLowerCase() === lowerSearchName;
      const bExact = b.name.toLowerCase() === lowerSearchName;
      if (aExact && !bExact) return -1;
      if (!aExact && bExact) return 1;
      
      // 然后按照匹配度排序
      const aStarts = a.name.toLowerCase().startsWith(lowerSearchName);
      const bStarts = b.name.toLowerCase().startsWith(lowerSearchName);
      if (aStarts && !bStarts) return -1;
      if (!aStarts && bStarts) return 1;
      
      return a.name.localeCompare(b.name);
    })
    .slice(0, maxResults);
}

/**
 * 获取Steam应用详情
 * @param {number} appId Steam应用ID
 * @returns {Promise<Object|null>} Steam应用详情
 */
export async function getSteamAppDetails(appId) {
  try {
    const response = await fetch(`/steam-store/api/appdetails?appids=${appId}&l=schinese`);
    const data = await response.json();
    
    const appData = data[appId];
    if (appData && appData.success && appData.data) {
      return appData.data;
    }
    return null;
  } catch (error) {
    console.warn(`无法获取Steam应用 ${appId} 的详情:`, error);
    return null;
  }
}

/**
 * 搜索Steam应用封面
 * @param {string} name 应用名称
 * @param {number} maxResults 最大结果数量
 * @returns {Promise<Array>} 封面列表
 */
export async function searchSteamCovers(name, maxResults = 20) {
  if (!name) {
    return [];
  }

  // 确保Steam应用列表已加载
  await loadSteamApps();

  // 搜索匹配的应用
  const matches = searchSteamApps(name, maxResults);

  // 并行获取应用详情
  const detailPromises = matches.map(async (app) => {
    const gameData = await getSteamAppDetails(app.appid);
    
    if (gameData) {
      return {
        name: gameData.name,
        appid: app.appid,
        source: 'steam',
        url: gameData.header_image || gameData.capsule_image || gameData.capsule_imagev5,
        saveUrl: getSteamCoverUrl(app.appid, 'library'),
        key: `steam_${app.appid}`,
        type: gameData.type || 'game',
        shortDescription: gameData.short_description || '',
        developers: gameData.developers || [],
        publishers: gameData.publishers || [],
        releaseDate: gameData.release_date || null
      };
    }
    return null;
  });

  const results = await Promise.all(detailPromises);
  return results.filter(item => item && item.url);
}

/**
 * 获取Steam封面图片URL
 * @param {number} appId Steam应用ID
 * @param {string} type 封面类型 ('header' | 'capsule' | 'library')
 * @returns {string} 封面图片URL
 */
export function getSteamCoverUrl(appId, type = 'header') {
  const baseUrl = 'https://steamcdn-a.akamaihd.net/steam/apps';
  
  switch (type) {
    case 'header':
      return `${baseUrl}/${appId}/header.jpg`;
    case 'capsule':
      return `${baseUrl}/${appId}/capsule_231x87.jpg`;
    case 'library':
      return `${baseUrl}/${appId}/library_600x900.jpg`;
    default:
      return `${baseUrl}/${appId}/header.jpg`;
  }
}

/**
 * 验证Steam应用ID
 * @param {number|string} appId 应用ID
 * @returns {boolean} 是否有效
 */
export function isValidSteamAppId(appId) {
  const id = parseInt(appId);
  return !isNaN(id) && id > 0 && id < 2147483647; // 32位整数最大值
}

/**
 * 格式化Steam应用信息
 * @param {Object} appData Steam应用数据
 * @returns {Object} 格式化后的应用信息
 */
export function formatSteamAppInfo(appData) {
  return {
    id: appData.steam_appid,
    name: appData.name,
    type: appData.type,
    description: appData.short_description,
    developers: appData.developers || [],
    publishers: appData.publishers || [],
    releaseDate: appData.release_date?.date || null,
    price: appData.price_overview || null,
    categories: appData.categories || [],
    genres: appData.genres || [],
    screenshots: appData.screenshots || [],
    movies: appData.movies || [],
    achievements: appData.achievements || [],
    platforms: appData.platforms || {},
    metacritic: appData.metacritic || null,
    recommendations: appData.recommendations || null
  };
}

export default {
  loadSteamApps,
  searchSteamApps,
  getSteamAppDetails,
  searchSteamCovers,
  getSteamCoverUrl,
  isValidSteamAppId,
  formatSteamAppInfo
}; 