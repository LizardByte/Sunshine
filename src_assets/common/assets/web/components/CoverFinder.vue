<template>
  <div class="dropdown-menu w-50 cover-finder overflow-hidden" :style="{ display: visible ? 'block' : 'none' }">
    <div class="modal-header px-3">
      <h4 class="modal-title">
        {{ coverSource === 'steam' ? 'Steam Store' : $t('apps.covers_found') }}
      </h4>
      <button type="button" class="btn-close me-2" @click="closeFinder"></button>
    </div>

    <!-- 封面来源选择器 -->
    <div class="cover-source-selector px-3">
      <div class="cover-source-tabs">
        <button
          class="cover-source-tab"
          :class="{ active: coverSource === 'igdb' }"
          @click.stop.prevent="setCoverSource('igdb')"
        >
          <i class="fas fa-gamepad me-1"></i>IGDB
        </button>
        <button
          class="cover-source-tab"
          :class="{ active: coverSource === 'steam' }"
          @click.stop.prevent="setCoverSource('steam')"
        >
          <i class="fab fa-steam me-1"></i>Steam Store
        </button>
      </div>
    </div>

    <div class="modal-body cover-results px-4 pt-2" :class="{ busy: loading }">
      <div class="row">
        <!-- 加载状态 -->
        <div v-if="loading" class="col-12">
          <div style="min-height: 100px">
            <div class="spinner-border" role="status">
              <span class="visually-hidden">{{ $t('apps.loading') }}</span>
            </div>
          </div>
        </div>

        <!-- 封面结果 -->
        <div
          v-for="(cover, index) in covers"
          :key="cover.key || `cover-${index}`"
          class="col-3 mb-3"
          @click="selectCover(cover)"
        >
          <div class="cover-container result">
            <img class="rounded" :src="cover.url" :alt="cover.name" />
          </div>
          <label class="steam-app-title d-block text-nowrap text-center text-truncate">
            {{ cover.name }}
          </label>
          <div v-if="cover.source === 'steam'" class="steam-app-info text-center">Steam ID: {{ cover.appid }}</div>
        </div>

        <!-- 无结果提示 -->
        <div v-if="!loading && covers.length === 0" class="col-12 text-center py-4">
          <i class="fas fa-search fa-3x text-muted mb-3"></i>
          <p class="text-muted">未找到相关封面</p>
          <p class="text-muted small">尝试使用不同的关键词或切换数据源</p>
        </div>
      </div>
    </div>
  </div>
</template>

<script>
import { searchSteamCovers } from '../utils/steamApi.js'

export default {
  name: 'CoverFinder',
  props: {
    visible: {
      type: Boolean,
      default: false,
    },
    searchTerm: {
      type: String,
      default: '',
    },
  },
  data() {
    return {
      coverSource: 'igdb',
      loading: false,
      covers: [],
    }
  },
  watch: {
    visible(newVal) {
      if (newVal && this.searchTerm) {
        this.searchCovers()
      }
    },
    searchTerm: {
      handler(newVal) {
        if (this.visible && newVal) {
          clearTimeout(this.debounceTimer)
          this.debounceTimer = setTimeout(() => {
            this.searchCovers()
          }, 500)
        }
      },
      immediate: true,
    },
  },
  methods: {
    /**
     * 设置封面来源
     */
    setCoverSource(source) {
      this.coverSource = source
      if (this.visible && this.searchTerm) {
        this.searchCovers()
      }
    },

    /**
     * 搜索封面
     */
    async searchCovers() {
      if (!this.searchTerm) {
        this.covers = []
        return
      }

      this.loading = true
      this.covers = []

      try {
        if (this.coverSource === 'steam') {
          this.covers = await searchSteamCovers(this.searchTerm)
        } else {
          this.covers = await this.searchIGDBCovers(this.searchTerm)
        }
      } catch (error) {
        console.error('搜索封面失败:', error)
        this.$emit('error', '搜索封面失败，请稍后重试')
      } finally {
        this.loading = false
      }
    },

    /**
     * 搜索IGDB封面
     */
    async searchIGDBCovers(name) {
      if (!name) {
        return []
      }

      const searchName = name.replaceAll(/\s+/g, '.').toLowerCase()
      const bucket = this.getSearchBucket(name)

      try {
        const response = await fetch(`https://db.lizardbyte.dev/buckets/${bucket}.json`)
        if (!response.ok) {
          throw new Error('Failed to search covers')
        }

        const maps = await response.json()
        const matchedIds = Object.keys(maps).filter((id) => {
          const item = maps[id]
          return item.name.replaceAll(/\s+/g, '.').toLowerCase().startsWith(searchName)
        })

        const gamePromises = matchedIds.map(async (id) => {
          try {
            const gameResponse = await fetch(`https://db.lizardbyte.dev/games/${id}.json`)
            return await gameResponse.json()
          } catch (error) {
            console.warn(`无法获取游戏 ${id} 的详情:`, error)
            return null
          }
        })

        const games = await Promise.all(gamePromises)

        return games
          .filter((game) => game && game.cover && game.cover.url)
          .map((game) => {
            const thumb = game.cover.url
            const dotIndex = thumb.lastIndexOf('.')
            const slashIndex = thumb.lastIndexOf('/')

            if (dotIndex < 0 || slashIndex < 0) {
              return null
            }

            const hash = thumb.substring(slashIndex + 1, dotIndex)
            return {
              name: game.name,
              key: `igdb_${game.id}`,
              source: 'igdb',
              url: `https://images.igdb.com/igdb/image/upload/t_cover_big/${hash}.jpg`,
              saveUrl: `https://images.igdb.com/igdb/image/upload/t_cover_big_2x/${hash}.png`,
            }
          })
          .filter((item) => item)
      } catch (error) {
        console.error('搜索IGDB封面失败:', error)
        return []
      }
    },

    /**
     * 获取搜索分桶
     */
    getSearchBucket(name) {
      const bucket = name
        .substring(0, Math.min(name.length, 2))
        .toLowerCase()
        .replaceAll(/[^a-z\d\u4e00-\u9fa5\u3400-\u4DBF\u20000-\u2A6DF]/g, '')

      return bucket || '@'
    },

    /**
     * 选择封面
     */
    async selectCover(cover) {
      this.$emit('loading', true)

      try {
        if (cover.source === 'steam') {
          // 直接使用Steam封面URL
          this.$emit('cover-selected', {
            path: cover.saveUrl,
            source: 'steam',
          })
        } else {
          // IGDB封面需要下载
          const response = await fetch('/api/covers/upload', {
            method: 'POST',
            headers: {
              'Content-Type': 'application/json',
            },
            body: JSON.stringify({
              key: cover.key,
              url: cover.saveUrl,
            }),
          })

          if (!response.ok) {
            throw new Error('Failed to download cover')
          }

          const body = await response.json()
          this.$emit('cover-selected', {
            path: body.path,
            source: 'igdb',
          })
        }

        this.closeFinder()
      } catch (error) {
        console.error('使用封面失败:', error)
        this.$emit('error', '使用封面失败，请稍后重试')
      } finally {
        this.$emit('loading', false)
      }
    },

    /**
     * 关闭查找器
     */
    closeFinder() {
      this.$emit('close')
    },
  },
}
</script>

<style scoped>
.cover-finder {
  max-height: 600px;
  overflow-y: auto;
}

.cover-results {
  max-height: 400px;
  overflow-x: hidden;
  overflow-y: auto;
}

.cover-results.busy * {
  cursor: wait !important;
  pointer-events: none;
}

.cover-container {
  padding-top: 133.33%;
  position: relative;
}

.cover-container.result {
  cursor: pointer;
}

.cover-container.result:hover {
  opacity: 0.8;
  transform: scale(1.02);
  transition: all 0.2s ease;
}

.spinner-border {
  position: absolute;
  left: 0;
  top: 0;
  right: 0;
  bottom: 0;
  margin: auto;
}

.cover-container img {
  display: block;
  position: absolute;
  top: 0;
  width: 100%;
  height: 100%;
  object-fit: cover;
}

.cover-source-selector {
  margin-bottom: 1rem;
}

.cover-source-tabs {
  display: flex;
  border-bottom: 1px solid #dee2e6;
  margin-bottom: 1rem;
}

.cover-source-tab {
  padding: 0.5rem 1rem;
  background: none;
  border: none;
  border-bottom: 2px solid transparent;
  cursor: pointer;
  color: #6c757d;
  transition: all 0.15s ease-in-out;
}

.cover-source-tab.active {
  color: #0d6efd;
  border-bottom-color: #0d6efd;
}

.cover-source-tab:hover {
  color: #0d6efd;
}

.steam-app-title {
  font-size: 0.75rem;
  margin-top: 0.5rem;
}

.steam-app-info {
  font-size: 0.75rem;
  color: #6c757d;
  margin-top: 0.25rem;
}
</style>
