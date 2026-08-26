<template>
  <div class="my-4">
    <h1>{{ $t('featured.title') }}</h1>
    <p>{{ $t('featured.description') }}</p>
  </div>

  <!-- Category Filter -->
  <div class="toolbar mb-4">
    <fieldset class="btn-group border-0 p-0 m-0" aria-label="Category filter">
      <button type="button" class="btn btn-outline-primary" :class="{ active: selectedCategory === null }"
        @click="selectedCategory = null">
        {{ $t('_common.all') }}
      </button>
      <button v-for="category in categories" :key="category.id" type="button" class="btn btn-outline-primary"
        :class="{ active: selectedCategory === category.id }" @click="selectedCategory = category.id">
        {{ $t(`featured.categories.${category.originalId}`) }}
      </button>
    </fieldset>
  </div>

  <!-- Loading State -->
  <div v-if="loading" class="text-center py-5">
    <LoadingSpinner :label="$t('_common.loading')" />
  </div>

  <!-- Error State -->
  <AlertBox v-else-if="error" variant="danger">
    {{ $t('_common.error') }}
    <template #body>
      <p class="mb-0">{{ error }}</p>
    </template>
  </AlertBox>

  <!-- Apps Grid -->
  <div v-else class="row g-4">
    <div v-for="app in filteredApps" :key="app.id" class="col-12 col-md-6 col-lg-4">
      <FeaturedAppCard :app="app" @open-screenshot="openScreenshot" />
    </div>
  </div>

  <!-- Empty State -->
  <div v-if="!loading && !error && filteredApps.length === 0" class="text-center py-5">
    <p class="text-muted">{{ $t('featured.no_apps') }}</p>
  </div>

  <!-- Screenshot Modal -->
  <ScreenshotModal v-if="activeScreenshots" :screenshots="activeScreenshots" :start-index="activeScreenshotIndex"
    @close="closeScreenshot" />
</template>

<script>
import AlertBox from '@/components/AlertBox.vue'
import FeaturedAppCard from '@/components/FeaturedAppCard.vue'
import LoadingSpinner from '@/components/LoadingSpinner.vue'
import ScreenshotModal from '@/components/ScreenshotModal.vue'

export default {
  components: {
    AlertBox,
    FeaturedAppCard,
    LoadingSpinner,
    ScreenshotModal,
  },
  data() {
    return {
      apps: [],
      categories: [],
      selectedCategory: null,
      loading: true,
      error: null,
      activeScreenshots: null,
      activeScreenshotIndex: 0,
    };
  },
  computed: {
    filteredApps() {
      let filtered = this.selectedCategory
        ? this.apps.filter(app => app.category === this.selectedCategory)
        : this.apps;

      // Sort by official status first, then by GitHub stars
      return filtered.slice().sort((a, b) => {
        // Official apps first
        if (a.official && !b.official) return -1;
        if (!a.official && b.official) return 1;

        // Then sort by GitHub stars (descending)
        const aStars = a.github?.stars || 0;
        const bStars = b.github?.stars || 0;
        return bStars - aStars;
      });
    }
  },
  created() {
    this.loadFeaturedApps();
  },
  methods: {
    async loadFeaturedApps() {
      try {
        this.loading = true;
        this.error = null;

        // Fetch the app directory for Sunshine
        const indexUrl = 'https://app.lizardbyte.dev/app-directory/sunshine.json';

        const response = await fetch(indexUrl);
        if (!response.ok) {
          throw new Error('Failed to load featured apps');
        }

        const data = await response.json();
        this.apps = data.apps || [];
        this.categories = data.categories || [];
      } catch (err) {
        console.error('Error loading featured apps:', err);
        this.error = err.message;
      } finally {
        this.loading = false;
      }
    },
    openScreenshot(url, screenshots) {
      this.activeScreenshots = screenshots || [];
      this.activeScreenshotIndex = this.activeScreenshots.indexOf(url);
    },
    closeScreenshot() {
      this.activeScreenshots = null;
    },
  },
}
</script>
