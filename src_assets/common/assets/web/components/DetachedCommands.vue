<template>
  <div class="form-group-enhanced">
    <label class="form-label-enhanced">{{ $t('apps.detached_cmds') }}</label>
    <div class="field-hint">
      {{ $t('apps.detached_cmds_desc') }}<br>
      <strong>{{ $t('_common.note') }}</strong> {{ $t('apps.detached_cmds_note') }}
    </div>
    
    <div v-if="commands.length > 0" class="detached-commands-list">
      <div 
        v-for="(command, index) in commands" 
        :key="`detached-${index}`" 
        class="d-flex align-items-center my-2"
      >
        <div class="flex-grow-1 me-2">
          <input 
            type="text" 
            v-model="commands[index]" 
            class="form-control form-control-enhanced monospace" 
            :placeholder="`独立命令 ${index + 1}`"
          />
        </div>
        <button 
          type="button" 
          class="btn btn-outline-danger btn-sm" 
          @click="removeCommand(index)"
          :title="'删除独立命令'"
        >
          <i class="fas fa-times"></i>
        </button>
      </div>
    </div>
    
    <button 
      type="button" 
      class="btn btn-outline-success add-command-btn" 
      @click="addCommand"
    >
      <i class="fas fa-plus me-1"></i>{{ $t('apps.detached_cmds_add') }}
    </button>
  </div>
</template>

<script>
export default {
  name: 'DetachedCommands',
  props: {
    commands: {
      type: Array,
      required: true
    }
  },
  methods: {
    /**
     * 添加独立命令
     */
    addCommand() {
      this.$emit('add-command');
    },
    
    /**
     * 移除独立命令
     */
    removeCommand(index) {
      this.$emit('remove-command', index);
    }
  }
};
</script>

<style scoped>
.field-hint {
  font-size: 0.875rem;
  color: #6c757d;
  margin-top: 0.25rem;
}

.monospace {
  font-family: monospace;
}

.detached-commands-list {
  background: rgba(255, 255, 255, 0.1);
  border: 1px solid rgba(255, 255, 255, 0.2);
  border-radius: 20px;
  padding: 1.5rem;
  margin-top: 1rem;
  backdrop-filter: blur(20px);
  box-shadow: 0 8px 32px rgba(0, 0, 0, 0.2);
}

.btn-sm {
  padding: 0.25rem 0.5rem;
  font-size: 0.875rem;
}

.btn-outline-danger:hover {
  background-color: #dc3545;
  border-color: #dc3545;
}

.btn-outline-success:hover {
  background-color: #198754;
  border-color: #198754;
}

/* 响应式设计 */
@media (max-width: 576px) {
  .d-flex {
    flex-direction: column;
  }
  
  .flex-grow-1 {
    margin-right: 0 !important;
    margin-bottom: 0.5rem;
  }
  
  .btn-sm {
    align-self: flex-end;
  }
}
</style> 