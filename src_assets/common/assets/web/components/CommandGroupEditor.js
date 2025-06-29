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
    if (!props.availableStages) return ''
    const stage = props.availableStages.find(s => s.id === stageId)
    return stage ? stage.description : ''
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
      case 'STREAM_TERMINATION':
        return 'Stream Termination'
      case 'POST_STREAM_START':
        return 'After Stream Start'
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
