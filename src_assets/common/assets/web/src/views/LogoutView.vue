<template>
  <div class="row justify-content-center py-5">
    <div class="col-md-8 col-lg-6">
      <div class="card">
        <div class="card-body text-center">
          <h1 class="h3 mb-3">{{ $t('logout.logged_out') }}</h1>
          <p class="text-muted mb-4">{{ $t('logout.logged_out_desc') }}</p>
          <a class="btn btn-primary" href="./">
            <log-in :size="18" class="icon"></log-in>
            {{ $t('logout.login') }}
          </a>
        </div>
      </div>
    </div>
  </div>
</template>

<script>
import { LogIn } from '@lucide/vue'

export default {
  components: {
    LogIn,
  },
}
</script>
