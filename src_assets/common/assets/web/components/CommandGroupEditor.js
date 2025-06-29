import { ref, computed, watch } from 'vue'

export function useCommandGroupEditor(props, emit) {
  // Component state
  const expandedGroups = ref(new Set())
  const showCommandHelp = ref(false)

  // Reactive data
  const localGroups = computed({
    get() {
      return props.groups || []
    },
    set(value) {
      emit('update', value)
    }
  })

  // Initialize expanded state for existing groups
  watch(() => props.groups, (newGroups) => {
    if (newGroups && newGroups.length > 0) {
      // Auto-expand first group if none are expanded
      if (expandedGroups.value.size === 0) {
        expandedGroups.value.add(0)
      }
      // Ensure showHelp property exists for each command
      newGroups.forEach(group => {
        if (group.commands) {
          group.commands.forEach(cmd => {
            if (typeof cmd.showHelp === 'undefined') cmd.showHelp = false
          })
        }
      })
    }
  }, { immediate: true })

  // Methods
  function updateGroup(groupIndex, field, value) {
    const updated = [...localGroups.value]
    updated[groupIndex][field] = value
    localGroups.value = updated
  }

  function updateCommand(groupIndex, commandIndex, field, value) {
    const updated = [...localGroups.value]
    if (!updated[groupIndex].commands) updated[groupIndex].commands = []
    updated[groupIndex].commands[commandIndex][field] = value
    localGroups.value = updated
  }

  function getStageDescription(stageId) {
    switch (stageId) {
      case 'PRE_STREAM_START':
        return 'Run before the stream starts. Use for setup tasks.'
      case 'POST_STREAM_START':
        return 'Run after the stream has started.'
      case 'PRE_DISPLAY_CHECK':
        return 'Run before display checks are performed.'
      case 'POST_DISPLAY_CHECK':
        return 'Run after display checks are complete.'
      case 'CLIENT_CONNECT':
        return 'Run when a client connects.'
      case 'ADDITIONAL_CLIENT':
        return 'Run when an additional client connects.'
      case 'STREAM_RESUME':
        return 'Run when the stream resumes from pause.'
      case 'STREAM_PAUSE':
        return 'Run when the stream is paused.'
      case 'CLIENT_DISCONNECT':
        return 'Run when a client disconnects.'
      case 'PRE_STREAM_STOP':
        return 'Run before the stream stops.'
      case 'PRE_DISPLAY_CLEANUP':
        return 'Run before display cleanup tasks.'
      case 'POST_DISPLAY_CLEANUP':
        return 'Run after display cleanup tasks.'
      case 'POST_STREAM_STOP':
        return 'Run after the stream has stopped.'
      case 'ADDITIONAL_CLIENT_DISCONNECT':
        return 'Run when an additional client disconnects.'
      case 'STREAM_TERMINATION':
        return 'Run at the end of the stream (termination).' 
      default:
        return ''
    }
  }

  function addGroup() {
    const newGroup = {
      name: props.simpleMode ? 
        getStageDisplayName() : 
        `Group ${localGroups.value.length + 1}`,
      failure_policy: getDefaultFailurePolicy(),
      commands: [],
      stage: props.availableStages && props.availableStages.length > 0 ? 
        props.availableStages[0].id : 
        props.stage
    }
    
    const updated = [...localGroups.value, newGroup]
    localGroups.value = updated
    
    // Auto-expand the new group
    expandedGroups.value.add(updated.length - 1)
  }

  function removeGroup(groupIndex) {
    if (confirm('Are you sure you want to remove this command group?')) {
      const updated = [...localGroups.value]
      updated.splice(groupIndex, 1)
      localGroups.value = updated
      
      // Update expanded state
      expandedGroups.value.delete(groupIndex)
      // Shift down expanded indices for groups after the removed one
      const newExpanded = new Set()
      expandedGroups.value.forEach(index => {
        if (index > groupIndex) {
          newExpanded.add(index - 1)
        } else if (index < groupIndex) {
          newExpanded.add(index)
        }
      })
      expandedGroups.value = newExpanded
    }
  }

  // Add showHelp property to new commands
  function addCommand(groupIndex) {
    const newCommand = {
      cmd: '',
      elevated: false,
      timeout_seconds: 30,
      ignore_error: false,
      async: false,
      showHelp: false
    }
    
    const updated = [...localGroups.value]
    if (!updated[groupIndex].commands) {
      updated[groupIndex].commands = []
    }
    updated[groupIndex].commands.push(newCommand)
    localGroups.value = updated
    
    // Ensure group is expanded
    expandedGroups.value.add(groupIndex)
  }

  function removeCommand(groupIndex, commandIndex) {
    if (confirm('Are you sure you want to remove this command?')) {
      const updated = [...localGroups.value]
      updated[groupIndex].commands.splice(commandIndex, 1)
      localGroups.value = updated
    }
  }

  function moveCommand(groupIndex, commandIndex, direction) {
    const updated = [...localGroups.value]
    const commands = updated[groupIndex].commands
    const newIndex = commandIndex + direction
    
    if (newIndex >= 0 && newIndex < commands.length) {
      [commands[commandIndex], commands[newIndex]] = [commands[newIndex], commands[commandIndex]]
      localGroups.value = updated
    }
  }

  function toggleGroupExpanded(groupIndex) {
    if (expandedGroups.value.has(groupIndex)) {
      expandedGroups.value.delete(groupIndex)
    } else {
      expandedGroups.value.add(groupIndex)
    }
  }

  function isGroupExpanded(groupIndex) {
    return expandedGroups.value.has(groupIndex)
  }

  function getDefaultFailurePolicy() {
    // Use continue for cleanup, fail fast for others
    return props.stage === 'STREAM_TERMINATION' ? 'CONTINUE_ON_FAILURE' : 'FAIL_FAST'
  }

  function getCommandPlaceholder() {
    if (props.platform === 'windows') {
      return 'e.g., taskkill /f /im "notepad.exe"'
    } else {
      return 'e.g., pkill firefox'
    }
  }

  function getPolicyDescription(policy) {
    switch (policy) {
      case 'FAIL_FAST':
        return 'Stop immediately on first failure'
      case 'CONTINUE_ON_FAILURE':
        return 'Continue execution even if commands fail, report success'
      case 'FAIL_STAGE_ON_ANY':
        return 'Execute ALL commands but report stage failure if any command fails'
      default:
        return ''
    }
  }

  function formatCommandsCount(count) {
    if (!count || count === 0) return '0'
    return count === 1 ? '1 command' : `${count} commands`
  }

  function getAllEnumDisplayName(value, type) {
    if (type === 'stage') {
      return getStageDisplayName(value)
    } else if (type === 'policy') {
      return getPolicyDisplayName(value)
    }
    return value
  }

  function getStageDisplayName(stageId = null) {
    const stage = stageId || props.stage
    switch (stage) {
      case 'PRE_STREAM_START':
        return 'Before Stream Start'
      case 'POST_STREAM_START':
        return 'After Stream Start'
      case 'PRE_DISPLAY_CHECK':
        return 'Before Display Check'
      case 'POST_DISPLAY_CHECK':
        return 'After Display Check'
      case 'CLIENT_CONNECT':
        return 'Client Connect'
      case 'ADDITIONAL_CLIENT':
        return 'Additional Client Connect'
      case 'STREAM_RESUME':
        return 'Stream Resume'
      case 'STREAM_PAUSE':
        return 'Stream Pause'
      case 'CLIENT_DISCONNECT':
        return 'Client Disconnect'
      case 'PRE_STREAM_STOP':
        return 'Before Stream Stop'
      case 'PRE_DISPLAY_CLEANUP':
        return 'Before Display Cleanup'
      case 'POST_DISPLAY_CLEANUP':
        return 'After Display Cleanup'
      case 'POST_STREAM_STOP':
        return 'After Stream Stop'
      case 'ADDITIONAL_CLIENT_DISCONNECT':
        return 'Additional Client Disconnect'
      case 'STREAM_TERMINATION':
        return 'Stream Termination'
      default:
        return stage || 'Unknown Stage'
    }
  }

  function getPolicyDisplayName(policy) {
    switch (policy) {
      case 'FAIL_FAST':
        return 'Fail Fast'
      case 'CONTINUE_ON_FAILURE':
        return 'Continue on Failure'
      case 'FAIL_STAGE_ON_ANY':
        return 'Fail Stage on Any Failure'
      default:
        return policy || 'Unknown Policy'
    }
  }

  function getFriendlyStageNames() {
    return {
      'PRE_STREAM_START': 'Before Stream Start',
      'STREAM_TERMINATION': 'Stream Termination', 
      'POST_STREAM_START': 'After Stream Start'
    }
  }

  function getFriendlyPolicyNames() {
    return {
      'FAIL_FAST': 'Fail Fast',
      'CONTINUE_ON_FAILURE': 'Continue on Failure',
      'FAIL_STAGE_ON_ANY': 'Fail Stage on Any Failure'
    }
  }

  return {
    expandedGroups,
    showCommandHelp,
    localGroups,
    updateGroup,
    updateCommand,
    getStageDescription,
    addGroup,
    removeGroup,
    addCommand,
    removeCommand,
    moveCommand,
    toggleGroupExpanded,
    isGroupExpanded,
    getDefaultFailurePolicy,
    getCommandPlaceholder,
    getPolicyDescription,
    formatCommandsCount,
    getAllEnumDisplayName,
    getStageDisplayName,
    getPolicyDisplayName,
    getFriendlyStageNames,
    getFriendlyPolicyNames
  }
}
