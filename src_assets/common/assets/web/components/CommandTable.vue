<template>
  <div class="form-group-enhanced">
    <label class="form-label-enhanced">{{ tableTitle }}</label>
    <div class="field-hint">{{ tableDescription }}</div>
    
    <div class="command-table" v-if="commands.length > 0">
      <table class="table table-sm">
        <thead>
          <tr>
            <th v-if="type === 'prep'">
              <i class="fas fa-play"></i> {{ $t('_common.do_cmd') }}
            </th>
            <th v-if="type === 'prep'">
              <i class="fas fa-undo"></i> {{ $t('_common.undo_cmd') }}
            </th>
            <th v-if="type === 'menu'">
              <i class="fas fa-tag"></i> 展示名称
            </th>
            <th v-if="type === 'menu'">
              <i class="fas fa-terminal"></i> 指令
            </th>
            <th v-if="platform === 'windows'">
              <i class="fas fa-shield-alt"></i> {{ $t('_common.run_as') }}
            </th>
            <th style="width: 100px;">操作</th>
          </tr>
        </thead>
        <tbody>
          <tr v-for="(command, index) in commands" :key="getCommandKey(command, index)">
            <!-- 准备命令字段 -->
            <template v-if="type === 'prep'">
              <td>
                <input 
                  type="text" 
                  class="form-control form-control-sm monospace" 
                  v-model="command.do" 
                  placeholder="执行命令"
                />
              </td>
              <td>
                <input 
                  type="text" 
                  class="form-control form-control-sm monospace" 
                  v-model="command.undo" 
                  placeholder="撤销命令"
                />
              </td>
            </template>
            
            <!-- 菜单命令字段 -->
            <template v-if="type === 'menu'">
              <td>
                <input 
                  type="text" 
                  class="form-control form-control-sm" 
                  v-model="command.name" 
                  placeholder="显示名称"
                />
              </td>
              <td>
                <input 
                  type="text" 
                  class="form-control form-control-sm monospace" 
                  v-model="command.cmd" 
                  placeholder="命令"
                />
              </td>
            </template>
            
            <!-- Windows权限设置 -->
            <td v-if="platform === 'windows'">
              <div class="form-check">
                <input 
                  type="checkbox" 
                  class="form-check-input" 
                  :id="`${type}-cmd-admin-${index}`" 
                  v-model="command.elevated"
                  true-value="true" 
                  false-value="false" 
                />
                <label :for="`${type}-cmd-admin-${index}`" class="form-check-label">
                  {{ $t('_common.elevated') }}
                </label>
              </div>
            </td>
            
            <!-- 操作按钮 -->
            <td>
              <button 
                type="button" 
                class="btn btn-outline-danger btn-sm" 
                @click="removeCommand(index)"
                :title="type === 'prep' ? '删除准备命令' : '删除菜单命令'"
              >
                <i class="fas fa-trash"></i>
              </button>
            </td>
          </tr>
        </tbody>
      </table>
    </div>
    
    <button 
      type="button" 
      class="btn btn-outline-success add-command-btn" 
      @click="addCommand"
    >
      <i class="fas fa-plus me-1"></i>{{ addButtonText }}
    </button>
  </div>
</template>

<script>
export default {
  name: 'CommandTable',
  props: {
    commands: {
      type: Array,
      required: true
    },
    platform: {
      type: String,
      default: 'linux'
    },
    type: {
      type: String,
      required: true,
      validator: value => ['prep', 'menu'].includes(value)
    }
  },
  computed: {
    tableTitle() {
      return this.type === 'prep' ? this.$t('apps.cmd_prep_name') : '炒鸡菜单命令';
    },
    tableDescription() {
      if (this.type === 'prep') {
        return this.$t('apps.cmd_prep_desc');
      }
      return '配置后在客户端返回菜单中可见，用于在不打断串流的情况下快速执行特定操作，例如调出辅助程序。\n示例：展示名称-关闭你的颠佬；指令-shutdown -s -t 10';
    },
    addButtonText() {
      return this.type === 'prep' ? this.$t('apps.add_cmds') : '添加菜单命令';
    }
  },
  methods: {
    /**
     * 获取命令的唯一键
     */
    getCommandKey(command, index) {
      if (this.type === 'menu' && command.id) {
        return `menu-${command.id}`;
      }
      return `${this.type}-${index}`;
    },
    
    /**
     * 添加命令
     */
    addCommand() {
      this.$emit('add-command');
    },
    
    /**
     * 移除命令
     */
    removeCommand(index) {
      this.$emit('remove-command', index);
    }
  }
};
</script>

<style scoped>
.monospace {
  font-family: 'Courier New', monospace;
}

.table {
  color: #ffffff;
  border-color: rgba(255, 255, 255, 0.15);
  margin-bottom: 0;
}

.table th {
  border-top: none;
  border-bottom: 2px solid rgba(255, 255, 255, 0.2);
  font-weight: 600;
  font-size: 0.875rem;
  padding: 1rem 0.75rem;
  background: rgba(255, 255, 255, 0.1);
  color: #ffffff;
  text-shadow: 0 1px 2px rgba(0, 0, 0, 0.3);
}

.table thead th:first-child {
  border-radius: 12px 0 0 0;
}

.table thead th:last-child {
  border-radius: 0 12px 0 0;
}

.table td {
  vertical-align: middle;
  border-color: rgba(255, 255, 255, 0.1);
  padding: 0.75rem;
  background: rgba(255, 255, 255, 0.05);
  transition: all 0.3s ease;
}

.table tbody tr:hover td {
  background: rgba(255, 255, 255, 0.1);
  backdrop-filter: blur(5px);
}

.table tbody tr:last-child td:first-child {
  border-radius: 0 0 0 12px;
}

.table tbody tr:last-child td:last-child {
  border-radius: 0 0 12px 0;
}

.form-control-sm {
  font-size: 0.875rem;
  background: rgba(255, 255, 255, 0.1);
  border: 1px solid rgba(255, 255, 255, 0.2);
  border-radius: 8px;
  color: #ffffff;
  backdrop-filter: blur(5px);
  transition: all 0.3s ease;
}

.form-control-sm:focus {
  background: rgba(255, 255, 255, 0.15);
  border-color: rgba(255, 255, 255, 0.4);
  box-shadow: 0 0 0 0.2rem rgba(255, 255, 255, 0.25);
  color: #ffffff;
}

.form-control-sm::placeholder {
  color: rgba(255, 255, 255, 0.6);
}

.btn-sm {
  padding: 0.5rem 0.75rem;
  font-size: 0.875rem;
  border-radius: 8px;
  transition: all 0.3s ease;
}

.form-check {
  display: flex;
  align-items: center;
  justify-content: center;
}

.form-check-input {
  background: rgba(255, 255, 255, 0.1);
  border: 1px solid rgba(255, 255, 255, 0.3);
  border-radius: 4px;
  backdrop-filter: blur(5px);
  transition: all 0.3s ease;
}

.form-check-input:checked {
  background: linear-gradient(135deg, #667eea, #764ba2);
  border-color: rgba(255, 255, 255, 0.5);
  box-shadow: 0 2px 8px rgba(102, 126, 234, 0.4);
}

.form-check-label {
  color: #ffffff;
  font-size: 0.875rem;
  font-weight: 500;
  margin-left: 0.5rem;
}

/* 动画效果 */
@keyframes fadeIn {
  from {
    opacity: 0;
    transform: translateY(20px);
  }
  to {
    opacity: 1;
    transform: translateY(0);
  }
}

/* 响应式设计 */
@media (max-width: 768px) {
  .command-table {
    padding: 1rem;
    margin-top: 0.5rem;
  }
  
  .table th,
  .table td {
    padding: 0.5rem;
    font-size: 0.8rem;
  }
  
  .form-control-sm {
    font-size: 0.8rem;
    padding: 0.25rem 0.5rem;
  }
  
  .btn-sm {
    padding: 0.25rem 0.5rem;
    font-size: 0.8rem;
  }
  
  .add-command-btn {
    padding: 0.5rem 1rem;
    font-size: 0.875rem;
  }
}
</style> 