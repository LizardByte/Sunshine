<template>
    <div class="card">
        <div class="card-body">
            <h2>{{ $t('resource_card.resources') }}</h2>
            <p>{{ $t('resource_card.resources_desc') }}</p>
            <div class="d-flex flex-wrap gap-2 mt-4">
                <a class="btn btn-success" href="https://app.lizardbyte.dev" target="_blank">
                  <Globe :size="18" class="icon"></Globe>
                  {{ $t('resource_card.lizardbyte_website') }}
                </a>
                <a class="btn btn-primary" href="https://app.lizardbyte.dev/discord" target="_blank">
                  <SimpleIcon icon="Discord" :size="18" class="icon"></SimpleIcon>
                  Discord
                </a>
                <a class="btn btn-secondary" href="https://github.com/orgs/LizardByte/discussions" target="_blank">
                  <SimpleIcon icon="GitHub" :size="18" class="icon"></SimpleIcon>
                  {{ $t('resource_card.github_discussions') }}
                </a>
            </div>
        </div>
    </div>
    <!-- Legal -->
    <div class="card mt-4">
        <div class="card-body">
            <h2>{{ $t('resource_card.legal') }}</h2>
            <p>{{ $t('resource_card.legal_desc') }}</p>
            <div class="d-flex flex-wrap gap-2 mt-4">
                <a class="btn btn-danger" href="https://github.com/LizardByte/Sunshine/blob/master/LICENSE"
                    target="_blank">
                  <FileText :size="18" class="icon"></FileText>
                  {{ $t('resource_card.license') }}
                </a>
                <a class="btn btn-danger" href="https://github.com/LizardByte/Sunshine/blob/master/NOTICE"
                    target="_blank">
                  <AlertCircle :size="18" class="icon"></AlertCircle>
                  {{ $t('resource_card.third_party_notice') }}
                </a>
            </div>
        </div>
    </div>
</template>

<script>
import {
  AlertCircle,
  FileText,
  Globe,
} from 'lucide-vue-next'
import SimpleIcon from './SimpleIcon.vue'

export default {
  components: {
    SimpleIcon,
    AlertCircle,
    FileText,
    Globe,
  }
}
</script>
