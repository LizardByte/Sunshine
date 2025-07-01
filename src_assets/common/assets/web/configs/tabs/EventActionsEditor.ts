import { ref, computed, watch, Ref } from 'vue'

export interface Command {
  cmd: string;
  elevated: boolean;
  timeout_seconds: number;
  ignore_error: boolean;
  async: boolean;
}

export interface Action {
  startup_stage: string;
  shutdown_stage: string;
  startup_commands: {
    failure_policy: 'FAIL_FAST' | 'CONTINUE_ON_FAILURE';
    commands: Command[];
  };
  cleanup_commands: {
    failure_policy: 'FAIL_FAST' | 'CONTINUE_ON_FAILURE';
    commands: Command[];
  };
}

export interface EventAction {
  name: string;
  action: Action;
}

export interface StageDefinition {
  stage: string;
  name: string;
  description: string;
  category: 'startup' | 'runtime' | 'cleanup';
}

export interface UseEventActionsEditorProps {
  modelValue: EventAction[];
}

export type UseEventActionsEditorEmit = (event: 'update:modelValue', value: EventAction[]) => void;

export default function useEventActionsEditor(
  props: UseEventActionsEditorProps,
  emit: UseEventActionsEditorEmit
) {
  const showNewActionModal = ref(false)
  const editingAction = ref(false)
  const editingActionIndex = ref(-1)
  const currentAction = ref<EventAction | null>(null)

  const eventActions = ref<EventAction[]>([])

  // Stage definitions
  const stageDefinitions: StageDefinition[] = [
    { stage: 'PRE_DISPLAY_CHECK', name: 'Pre Display Check', description: 'Before display validation', category: 'startup' },
    { stage: 'POST_DISPLAY_CHECK', name: 'Post Display Check', description: 'After display has been validated', category: 'startup' },
    { stage: 'PRE_STREAM_START', name: 'Pre Stream Start', description: 'Before the stream begins', category: 'startup' },
    { stage: 'POST_STREAM_START', name: 'Post Stream Start', description: 'After the stream has started successfully', category: 'runtime' },
    { stage: 'ADDITIONAL_CLIENT', name: 'Additional Client', description: 'When an additional client connects', category: 'runtime' },
    { stage: 'STREAM_RESUME', name: 'Stream Resume', description: 'When stream resumes from pause', category: 'runtime' },
    { stage: 'STREAM_PAUSE', name: 'Stream Pause', description: 'When stream is paused', category: 'cleanup' },
    { stage: 'ADDITIONAL_CLIENT_DISCONNECT', name: 'Additional Client Disconnect', description: 'When an additional client disconnects', category: 'cleanup' },
    { stage: 'PRE_STREAM_STOP', name: 'Pre Stream Stop', description: 'Before the stream stops', category: 'cleanup' },
    { stage: 'POST_STREAM_STOP', name: 'Post Stream Stop', description: 'After the stream has stopped', category: 'cleanup' }
  ]

  // Computed properties
  const startupStageOptions = computed(() => stageDefinitions.filter(s => s.category !== 'cleanup'))
  const cleanupStageOptions = computed(() => stageDefinitions.filter(s => s.category === 'cleanup'))

  // Watch for changes in modelValue
  watch(() => props.modelValue, (newValue) => {
    eventActions.value = Array.isArray(newValue) ? [...newValue] : []
  }, { immediate: true })

  // Methods
  function getStageDisplayName(stageName: string): string {
    const stage = stageDefinitions.find(s => s.stage === stageName)
    return stage ? `${stage.name}` : stageName
  }

  function showNewAction() {
    currentAction.value = createDefaultAction()
    editingAction.value = false
    editingActionIndex.value = -1
    showNewActionModal.value = true
  }

  function editAction(index: number) {
    const action = eventActions.value[index]
    currentAction.value = JSON.parse(JSON.stringify(action))
    editingAction.value = true
    editingActionIndex.value = index
    showNewActionModal.value = true
  }

  function deleteAction(index: number) {
    if (confirm('Are you sure you want to delete this action?')) {
      eventActions.value.splice(index, 1)
      updateModelValue()
    }
  }

  function saveCurrentAction() {
    if (!currentAction.value || !currentAction.value.name.trim()) {
      alert('Please enter an action name')
      return
    }
    if (!currentAction.value.action.startup_stage && !currentAction.value.action.shutdown_stage) {
      alert('Please select at least one stage (startup or shutdown)')
      return
    }
    currentAction.value.action.startup_commands.commands = currentAction.value.action.startup_commands.commands.filter(cmd => cmd.cmd.trim())
    currentAction.value.action.cleanup_commands.commands = currentAction.value.action.cleanup_commands.commands.filter(cmd => cmd.cmd.trim())
    if (editingAction.value) {
      eventActions.value[editingActionIndex.value] = currentAction.value
    } else {
      eventActions.value.push(currentAction.value)
    }
    updateModelValue()
    closeActionModal()
  }

  function closeActionModal() {
    showNewActionModal.value = false
    editingAction.value = false
    editingActionIndex.value = -1
    currentAction.value = null
  }

  function updateModelValue() {
    emit('update:modelValue', [...eventActions.value])
  }

  function createEmptyCommand(): Command {
    return {
      cmd: '',
      elevated: false,
      timeout_seconds: 30,
      ignore_error: false,
      async: false
    }
  }

  function createDefaultAction(): EventAction {
    return {
      name: '',
      action: {
        startup_stage: '',
        shutdown_stage: '',
        startup_commands: {
          failure_policy: 'FAIL_FAST',
          commands: [createEmptyCommand()]
        },
        cleanup_commands: {
          failure_policy: 'CONTINUE_ON_FAILURE',
          commands: []
        }
      }
    }
  }

  function addStartupCommand() {
    if (currentAction.value)
      currentAction.value.action.startup_commands.commands.push(createEmptyCommand())
  }

  function removeStartupCommand(index: number) {
    if (currentAction.value) {
      currentAction.value.action.startup_commands.commands.splice(index, 1)
    }
  }

  function addCleanupCommand() {
    if (currentAction.value) {
      const cleanupCommand = createEmptyCommand()
      cleanupCommand.ignore_error = true
      currentAction.value.action.cleanup_commands.commands.push(cleanupCommand)
    }
  }

  function removeCleanupCommand(index: number) {
    if (currentAction.value)
      currentAction.value.action.cleanup_commands.commands.splice(index, 1)
  }

  return {
    showNewActionModal,
    editingAction,
    editingActionIndex,
    currentAction,
    eventActions,
    stageDefinitions,
    startupStageOptions,
    cleanupStageOptions,
    getStageDisplayName,
    showNewAction,
    editAction,
    deleteAction,
    saveCurrentAction,
    closeActionModal,
    updateModelValue,
    createEmptyCommand,
    createDefaultAction,
    addStartupCommand,
    removeStartupCommand,
    addCleanupCommand,
    removeCleanupCommand,
  }
}
