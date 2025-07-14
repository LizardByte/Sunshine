<template>
  <div class="modal fade" id="editAppModal" tabindex="-1" aria-labelledby="editAppModalLabel" aria-hidden="true" ref="modalElement">
    <div class="modal-dialog modal-xl">
      <div class="modal-content">
        <div class="modal-header">
          <h5 class="modal-title" id="editAppModalLabel">
            <i class="fas fa-edit me-2"></i>
            {{ isNewApp ? '添加新应用' : '编辑应用' }}
          </h5>
          <button type="button" class="btn-close" data-bs-dismiss="modal" aria-label="Close"></button>
        </div>
        <div class="modal-body">
          <!-- 隐藏的文件选择输入 -->
          <input type="file" ref="fileInput" style="display: none" />
          <input type="file" ref="dirInput" style="display: none" webkitdirectory />
          
          <form @submit.prevent="saveApp" v-if="formData">
            <!-- 基础信息手风琴 -->
            <div class="accordion" id="appFormAccordion">
              <!-- 基本信息 -->
              <div class="accordion-item">
                <h2 class="accordion-header" id="basicInfoHeading">
                  <button class="accordion-button" type="button" data-bs-toggle="collapse" data-bs-target="#basicInfoCollapse" aria-expanded="true" aria-controls="basicInfoCollapse">
                    <i class="fas fa-info-circle me-2"></i>基本信息
                  </button>
                </h2>
                <div id="basicInfoCollapse" class="accordion-collapse collapse show" aria-labelledby="basicInfoHeading" data-bs-parent="#appFormAccordion">
                  <div class="accordion-body">
                    <!-- 应用名称 -->
                    <div class="form-group-enhanced">
                      <label for="appName" class="form-label-enhanced required-field">{{ $t('apps.app_name') }}</label>
                      <input 
                        type="text" 
                        class="form-control form-control-enhanced" 
                        id="appName" 
                        v-model="formData.name" 
                        :class="getFieldClass('name')"
                        @blur="validateField('name')"
                        required 
                      />
                      <div v-if="validation.name && !validation.name.isValid" class="invalid-feedback">
                        {{ validation.name.message }}
                      </div>
                      <div v-if="validation.name && validation.name.isValid && formData.name" class="valid-feedback">
                        应用名称有效
                      </div>
                      <div class="field-hint">{{ $t('apps.app_name_desc') }}</div>
                    </div>
                    
                    <!-- 输出名称 -->
                    <div class="form-group-enhanced">
                      <label for="appOutput" class="form-label-enhanced">{{ $t('apps.output_name') }}</label>
                      <input 
                        type="text" 
                        class="form-control form-control-enhanced monospace" 
                        id="appOutput" 
                        v-model="formData.output"
                        :class="getFieldClass('output')"
                        @blur="validateField('output')"
                      />
                      <div v-if="validation.output && !validation.output.isValid" class="invalid-feedback">
                        {{ validation.output.message }}
                      </div>
                      <div class="field-hint">{{ $t('apps.output_desc') }}</div>
                    </div>

                    <!-- 主命令 -->
                    <div class="form-group-enhanced">
                      <label for="appCmd" class="form-label-enhanced required-field">{{ $t('apps.cmd') }}</label>
                      <div class="input-group">
                        <input 
                          type="text" 
                          class="form-control form-control-enhanced monospace" 
                          id="appCmd" 
                          v-model="formData.cmd" 
                          :class="getFieldClass('cmd')"
                          @blur="validateField('cmd')"
                          :placeholder="getPlaceholderText('cmd')"
                          required 
                        />
                        <button class="btn btn-outline-secondary" type="button" @click="selectFile('cmd')" :title="getButtonTitle('file')">
                          <i class="fas fa-folder-open"></i>
                        </button>
                      </div>
                      <div v-if="validation.cmd && !validation.cmd.isValid" class="invalid-feedback">
                        {{ validation.cmd.message }}
                      </div>
                      <div v-if="validation.cmd && validation.cmd.isValid && formData.cmd" class="valid-feedback">
                        命令有效
                      </div>
                      <div class="field-hint">
                        {{ $t('apps.cmd_desc') }}<br>
                        <strong>{{ $t('_common.note') }}</strong> {{ $t('apps.cmd_note') }}
                      </div>
                    </div>

                    <!-- 工作目录 -->
                    <div class="form-group-enhanced">
                      <label for="appWorkingDir" class="form-label-enhanced">{{ $t('apps.working_dir') }}</label>
                      <div class="input-group">
                        <input 
                          type="text" 
                          class="form-control form-control-enhanced monospace" 
                          id="appWorkingDir" 
                          v-model="formData['working-dir']"
                          :class="getFieldClass('working-dir')"
                          @blur="validateField('working-dir')"
                          :placeholder="getPlaceholderText('working-dir')"
                        />
                        <button class="btn btn-outline-secondary" type="button" @click="selectDirectory('working-dir')" :title="getButtonTitle('directory')">
                          <i class="fas fa-folder-open"></i>
                        </button>
                      </div>
                      <div v-if="validation['working-dir'] && !validation['working-dir'].isValid" class="invalid-feedback">
                        {{ validation['working-dir'].message }}
                      </div>
                      <div class="field-hint">{{ $t('apps.working_dir_desc') }}</div>
                    </div>
                  </div>
                </div>
              </div>

              <!-- 命令设置 -->
              <div class="accordion-item">
                <h2 class="accordion-header" id="commandsHeading">
                  <button class="accordion-button collapsed" type="button" data-bs-toggle="collapse" data-bs-target="#commandsCollapse" aria-expanded="false" aria-controls="commandsCollapse">
                    <i class="fas fa-terminal me-2"></i>命令设置
                  </button>
                </h2>
                <div id="commandsCollapse" class="accordion-collapse collapse" aria-labelledby="commandsHeading" data-bs-parent="#appFormAccordion">
                  <div class="accordion-body">
                    <!-- 全局准备命令 -->
                    <div class="form-group-enhanced">
                      <div class="form-check form-switch">
                        <input
                          type="checkbox"
                          class="form-check-input"
                          id="excludeGlobalPrepSwitch"
                          v-model="formData['exclude-global-prep-cmd']"
                          :true-value="'true'"
                          :false-value="'false'"
                        >
                        <label class="form-check-label" for="excludeGlobalPrepSwitch">
                          {{ $t('apps.global_prep_name') }}
                        </label>
                      </div>
                      <div class="field-hint">{{ $t('apps.global_prep_desc') }}</div>
                    </div>

                    <!-- 准备命令 -->
                    <CommandTable 
                      :commands="formData['prep-cmd']" 
                      :platform="platform"
                      type="prep"
                      @add-command="addPrepCommand"
                      @remove-command="removePrepCommand"
                    />

                    <!-- 菜单命令 -->
                    <CommandTable 
                      :commands="formData['menu-cmd']" 
                      :platform="platform"
                      type="menu"
                      @add-command="addMenuCommand"
                      @remove-command="removeMenuCommand"
                    />

                    <!-- 独立命令 -->
                    <DetachedCommands 
                      :commands="formData.detached"
                      @add-command="addDetachedCommand"
                      @remove-command="removeDetachedCommand"
                    />
                  </div>
                </div>
              </div>

              <!-- 高级选项 -->
              <div class="accordion-item">
                <h2 class="accordion-header" id="advancedHeading">
                  <button class="accordion-button collapsed" type="button" data-bs-toggle="collapse" data-bs-target="#advancedCollapse" aria-expanded="false" aria-controls="advancedCollapse">
                    <i class="fas fa-cogs me-2"></i>高级选项
                  </button>
                </h2>
                <div id="advancedCollapse" class="accordion-collapse collapse" aria-labelledby="advancedHeading" data-bs-parent="#appFormAccordion">
                  <div class="accordion-body">
                    <!-- 权限设置 -->
                    <div class="form-group-enhanced" v-if="platform === 'windows'">
                      <div class="form-check">
                        <input type="checkbox" class="form-check-input" id="appElevation" v-model="formData.elevated"
                          true-value="true" false-value="false" />
                        <label for="appElevation" class="form-check-label">{{ $t('_common.run_as') }}</label>
                      </div>
                      <div class="field-hint">{{ $t('apps.run_as_desc') }}</div>
                    </div>

                    <!-- 自动分离 -->
                    <div class="form-group-enhanced">
                      <div class="form-check">
                        <input type="checkbox" class="form-check-input" id="autoDetach" v-model="formData['auto-detach']"
                          true-value="true" false-value="false" />
                        <label for="autoDetach" class="form-check-label">{{ $t('apps.auto_detach') }}</label>
                      </div>
                      <div class="field-hint">{{ $t('apps.auto_detach_desc') }}</div>
                    </div>

                    <!-- 等待所有进程 -->
                    <div class="form-group-enhanced">
                      <div class="form-check">
                        <input type="checkbox" class="form-check-input" id="waitAll" v-model="formData['wait-all']" 
                               true-value="true" false-value="false" />
                        <label for="waitAll" class="form-check-label">{{ $t('apps.wait_all') }}</label>
                      </div>
                      <div class="field-hint">{{ $t('apps.wait_all_desc') }}</div>
                    </div>

                    <!-- 退出超时 -->
                    <div class="form-group-enhanced">
                      <label for="exitTimeout" class="form-label-enhanced">{{ $t('apps.exit_timeout') }}</label>
                      <input 
                        type="number" 
                        class="form-control form-control-enhanced" 
                        id="exitTimeout" 
                        v-model="formData['exit-timeout']" 
                        min="0"
                        :class="getFieldClass('exit-timeout')"
                        @blur="validateField('exit-timeout')"
                      />
                      <div v-if="validation['exit-timeout'] && !validation['exit-timeout'].isValid" class="invalid-feedback">
                        {{ validation['exit-timeout'].message }}
                      </div>
                      <div class="field-hint">{{ $t('apps.exit_timeout_desc') }}</div>
                    </div>
                  </div>
                </div>
              </div>

              <!-- 图片设置 -->
              <div class="accordion-item">
                <h2 class="accordion-header" id="imageHeading">
                  <button class="accordion-button collapsed" type="button" data-bs-toggle="collapse" data-bs-target="#imageCollapse" aria-expanded="false" aria-controls="imageCollapse">
                    <i class="fas fa-image me-2"></i>图片设置
                  </button>
                </h2>
                <div id="imageCollapse" class="accordion-collapse collapse" aria-labelledby="imageHeading" data-bs-parent="#appFormAccordion">
                  <div class="accordion-body">
                    <ImageSelector 
                      :image-path="formData['image-path']"
                      :app-name="formData.name"
                      @update-image="updateImage"
                      @image-error="handleImageError"
                    />
                  </div>
                </div>
              </div>
            </div>
          </form>
        </div>
        <div class="modal-footer modal-footer-enhanced">
          <div class="save-status">
            <span v-if="isFormValid" class="text-success">
              <i class="fas fa-check-circle me-1"></i>合规应用
            </span>
            <span v-else class="text-warning">
              <i class="fas fa-exclamation-triangle me-1"></i>请检查必填字段
            </span>
            <div v-if="imageError" class="text-danger mt-1">
              <i class="fas fa-exclamation-circle me-1"></i>{{ imageError }}
            </div>
          </div>
          <div>
            <button type="button" class="btn btn-secondary me-2" @click="closeModal">
              <i class="fas fa-times me-1"></i>{{ $t('_common.cancel') }}
            </button>
            <button type="button" class="btn btn-primary" @click="saveApp" :disabled="disabled">
              <i class="fas fa-save me-1"></i>{{ $t('_common.save') }}
            </button>
          </div>
        </div>
      </div>
    </div>
  </div>
</template>

<script>
import { validateField, validateAppForm } from '../utils/validation.js';
import { nanoid } from 'nanoid';
import { Modal } from 'bootstrap';
import CommandTable from './CommandTable.vue';
import DetachedCommands from './DetachedCommands.vue';
import ImageSelector from './ImageSelector.vue';
import { createFileSelector } from '../utils/fileSelection.js';

export default {
  name: 'AppEditor',
  components: {
    CommandTable,
    DetachedCommands,
    ImageSelector
  },
  props: {
    app: {
      type: Object,
      default: null
    },
    platform: {
      type: String,
      default: 'linux'
    },
    disabled: {
      type: Boolean,
      default: false
    }
  },
  data() {
    return {
      formData: null,
      validation: {},
      imageError: '',
      modalInstance: null,
      fileSelector: null
    };
  },
  computed: {
    isNewApp() {
      return !this.app || this.app.index === -1;
    },
    isFormValid() {
      return this.validation.name?.isValid && this.validation.cmd?.isValid;
    }
  },
  watch: {
    app: {
      handler(newApp) {
        if (newApp) {
          this.initializeForm(newApp);
          this.$nextTick(() => {
            this.showModal();
          });
        }
      },
      immediate: true,
    }
  },
  mounted() {
    // 延迟初始化模态框实例，确保DOM完全渲染
    this.$nextTick(() => {
      this.initializeModal();
      this.initializeFileSelector();
    });
  },
  beforeUnmount() {
    // 清理模态框实例
    if (this.modalInstance) {
      this.modalInstance.dispose();
    }
    
    // 清理文件选择器
    if (this.fileSelector) {
      this.fileSelector.resetState();
      this.fileSelector.cleanupFileInputs(this.$refs.fileInput, this.$refs.dirInput);
    }
  },
  methods: {
    /**
     * 初始化模态框
     */
    initializeModal() {
      // 避免重复初始化
      if (this.modalInstance) {
        return;
      }
      
      if (!this.$refs.modalElement) {
        console.warn('Modal element not found');
        return;
      }

      try {
        // 使用全局的 bootstrap 对象
        this.modalInstance = new Modal(this.$refs.modalElement, {
          backdrop: 'static',
          keyboard: false
        });
        console.log('Modal initialized successfully');
      } catch (error) {
        console.warn('Modal initialization failed:', error);
      }
    },

    /**
     * 初始化文件选择器
     */
    initializeFileSelector() {
      this.fileSelector = createFileSelector({
        platform: this.platform,
        onSuccess: (message) => {
          this.showInfoMessage(message);
        },
        onError: (error) => {
          this.showErrorMessage(error);
        },
        onInfo: (info) => {
          this.showInfoMessage(info);
        }
      });
    },
    
    /**
     * 显示模态框
     */
    showModal() {
      console.log('Attempting to show modal');
      // 确保模态框已初始化
      if (!this.modalInstance) {
        this.initializeModal();
      }
      
      if (this.modalInstance) {
        try {
          this.modalInstance.show();
          console.log('Modal shown successfully');
        } catch (error) {
          console.error('Failed to show modal:', error);
        }
      } else {
        console.error('Modal instance not available');
      }
    },
    
    /**
     * 关闭模态框
     */
    closeModal() {
      if (this.modalInstance) {
        this.modalInstance.hide();
      }
      this.resetFileSelection();
      this.$emit('close');
    },

    /**
     * 重置文件选择状态
     */
    resetFileSelection() {
      if (this.fileSelector) {
        this.fileSelector.resetState();
        this.fileSelector.cleanupFileInputs(this.$refs.fileInput, this.$refs.dirInput);
      }
    },

    /**
     * 显示错误消息
     */
    showErrorMessage(message) {
      // 可以用更好的通知组件替换
      if (typeof window !== 'undefined' && window.showToast) {
        window.showToast(message, 'error');
      } else {
        console.error(message);
        alert(message);
      }
    },

    /**
     * 显示信息消息
     */
    showInfoMessage(message) {
      // 可以用更好的通知组件替换
      if (typeof window !== 'undefined' && window.showToast) {
        window.showToast(message, 'info');
      } else {
        console.info(message);
      }
    },


    
    /**
     * 初始化表单数据
     */
    initializeForm(app) {
      if (app.index === -1) {
        // 新应用
        this.formData = {
          name: "",
          output: "",
          cmd: "",
          index: -1,
          "exclude-global-prep-cmd": false,
          elevated: false,
          "auto-detach": true,
          "wait-all": true,
          "exit-timeout": 5,
          "prep-cmd": [],
          "menu-cmd": [],
          detached: [],
          "image-path": "",
          "working-dir": ""
        };
      } else {
        // 编辑现有应用
        this.formData = JSON.parse(JSON.stringify(app));
        this.ensureDefaultValues();
      }
      
      // 重置验证状态
      this.validation = {};
      this.imageError = '';
    },
    
    /**
     * 确保默认值存在
     */
    ensureDefaultValues() {
      if (!this.formData["prep-cmd"]) this.formData["prep-cmd"] = [];
      if (!this.formData["menu-cmd"]) this.formData["menu-cmd"] = [];
      if (!this.formData["detached"]) this.formData["detached"] = [];
      if (!this.formData["exclude-global-prep-cmd"]) this.formData["exclude-global-prep-cmd"] = false;
      if (this.formData["elevated"] === undefined && this.platform === 'windows') {
        this.formData["elevated"] = false;
      }
      if (this.formData["auto-detach"] === undefined) {
        this.formData["auto-detach"] = true;
      }
      if (this.formData["wait-all"] === undefined) {
        this.formData["wait-all"] = true;
      }
      if (this.formData["exit-timeout"] === undefined) {
        this.formData["exit-timeout"] = 5;
      }
      if (!this.formData["working-dir"]) this.formData["working-dir"] = "";
    },
    
    /**
     * 验证单个字段
     */
    validateField(fieldName) {
      const fieldMap = {
        'name': 'appName',
        'cmd': 'command',
        'output': 'outputName',
        'working-dir': 'workingDir',
        'exit-timeout': 'timeout',
        'image-path': 'imagePath'
      };
      
      const validationKey = fieldMap[fieldName] || fieldName;
      const result = validateField(validationKey, this.formData[fieldName]);
      
      // Vue 3响应式更新
      this.validation[fieldName] = result;
      
      return result;
    },
    
    /**
     * 获取字段CSS类
     */
    getFieldClass(fieldName) {
      const validation = this.validation[fieldName];
      if (!validation) return '';
      
      return {
        'is-invalid': !validation.isValid,
        'is-valid': validation.isValid && this.formData[fieldName]
      };
    },
    
    /**
     * 添加准备命令
     */
    addPrepCommand() {
      const cmd = { do: "", undo: "" };
      if (this.platform === 'windows') {
        cmd.elevated = false;
      }
      this.formData['prep-cmd'].push(cmd);
    },
    
    /**
     * 移除准备命令
     */
    removePrepCommand(index) {
      this.formData['prep-cmd'].splice(index, 1);
    },
    
    /**
     * 添加菜单命令
     */
    addMenuCommand() {
      const cmd = { id: nanoid(10), name: "", cmd: "" };
      if (this.platform === 'windows') {
        cmd.elevated = false;
      }
      this.formData['menu-cmd'].push(cmd);
    },
    
    /**
     * 移除菜单命令
     */
    removeMenuCommand(index) {
      this.formData['menu-cmd'].splice(index, 1);
    },
    
    /**
     * 添加独立命令
     */
    addDetachedCommand() {
      this.formData.detached.push('');
    },
    
    /**
     * 移除独立命令
     */
    removeDetachedCommand(index) {
      this.formData.detached.splice(index, 1);
    },
    
    /**
     * 更新图片路径
     */
    updateImage(imagePath) {
      this.formData['image-path'] = imagePath;
      this.imageError = '';
    },
    
    /**
     * 处理图片错误
     */
    handleImageError(error) {
      this.imageError = error;
    },

    /**
     * 选择文件
     */
    selectFile(fieldName) {
      if (!this.fileSelector) {
        this.showErrorMessage('文件选择器未初始化');
        return;
      }
      
      this.fileSelector.selectFile(
        fieldName,
        this.$refs.fileInput,
        this.onFilePathSelected
      );
    },

    /**
     * 选择目录
     */
    selectDirectory(fieldName) {
      if (!this.fileSelector) {
        this.showErrorMessage('文件选择器未初始化');
        return;
      }
      
      this.fileSelector.selectDirectory(
        fieldName,
        this.$refs.dirInput,
        this.onFilePathSelected
      );
    },

    /**
     * 文件路径选择完成回调
     */
    onFilePathSelected(fieldName, filePath) {
      this.formData[fieldName] = filePath;
      this.validateField(fieldName);
    },

    /**
     * 获取字段占位符文本
     */
    getPlaceholderText(fieldName) {
      return this.fileSelector ? this.fileSelector.getPlaceholderText(fieldName) : '';
    },

    /**
     * 获取按钮标题文本
     */
    getButtonTitle(type) {
      return this.fileSelector ? this.fileSelector.getButtonTitle(type) : '选择';
    },
    
    /**
     * 保存应用
     */
    async saveApp() {
      // 验证所有字段
      const formValidation = validateAppForm(this.formData);
      
      if (!formValidation.isValid) {
        // 显示第一个错误
        if (formValidation.errors.length > 0) {
          alert(formValidation.errors[0]);
        }
        return;
      }
      
      // 处理图片路径
      const editedApp = {
        ...this.formData,
        ...(this.formData["image-path"] && {
          "image-path": this.formData["image-path"].toString().replace(/"/g, '')
        })
      };
      
      this.$emit('save-app', editedApp);
    }
  }
};
</script>

<style scoped>
.required-field::after {
  content: " *";
  color: #dc3545;
}

.modal-footer-enhanced {
  border-top: 1px solid #dee2e6;
  padding: 1rem 1.5rem;
  display: flex;
  justify-content: space-between;
  align-items: center;
}

.save-status {
  font-size: 0.875rem;
  color: #6c757d;
}

.is-invalid {
  border-color: #dc3545;
}

.is-valid {
  border-color: #198754;
}

.invalid-feedback {
  display: block;
  font-size: 0.875rem;
  color: #dc3545;
  margin-top: 0.25rem;
}

.valid-feedback {
  display: block;
  font-size: 0.875rem;
  color: #198754;
  margin-top: 0.25rem;
}

.monospace {
  font-family: monospace;
}
</style> 