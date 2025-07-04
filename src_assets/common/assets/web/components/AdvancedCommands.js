import { ref, computed, onMounted, watch } from 'vue'

export function useAdvancedCommands(props, emit) {
  // Component state - simplified
  const isAdvancedMode = ref(false) // Track if user has switched to advanced mode
  const showAdvancedModal = ref(false) // Control modal visibility

  // Stage definitions for Start and Cleanup sections
  const startStages = [
    { 
      id: 'PRE_STREAM_START', 
      order: 0, 
      icon: 'fas fa-play-circle text-primary',
      name: 'Before Stream Start',
      description: 'Commands executed before the stream begins'
    },
    { 
      id: 'POST_STREAM_START', 
      order: 1, 
      icon: 'fas fa-check-circle text-success',
      name: 'After Stream Start',
      description: 'Commands executed after the stream has started successfully'
    },
    { 
      id: 'PRE_DISPLAY_CHECK', 
      order: 2, 
      icon: 'fas fa-desktop text-info',
      name: 'Before Display Check',
      description: 'Commands executed before display validation'
    },
    { 
      id: 'POST_DISPLAY_CHECK', 
      order: 3, 
      icon: 'fas fa-monitor-waveform text-info',
      name: 'After Display Check',
      description: 'Commands executed after display has been validated'
    },
    { 
      id: 'ADDITIONAL_CLIENT', 
      order: 4, 
      icon: 'fas fa-user-plus text-primary',
      name: 'New Client Connected',
      description: 'Commands executed when an additional client connects'
    },
    { 
      id: 'STREAM_RESUME', 
      order: 5, 
      icon: 'fas fa-play-circle text-warning',
      name: 'Stream Resume',
      description: 'Commands executed when stream resumes from pause'
    }
  ]

  const cleanupStages = [
    { 
      id: 'STREAM_PAUSE', 
      order: 0, 
      icon: 'fas fa-pause-circle text-warning',
      name: 'Stream Pause',
      description: 'Commands executed when stream is paused'
    },
    { 
      id: 'PRE_STREAM_STOP', 
      order: 1, 
      icon: 'fas fa-pause-circle text-warning',
      name: 'Before Stream Stop',
      description: 'Commands executed before the stream stops'
    },
    { 
      id: 'PRE_DISPLAY_CLEANUP', 
      order: 2, 
      icon: 'fas fa-broom text-warning',
      name: 'Before Display Cleanup',
      description: 'Commands executed before display cleanup'
    },
    { 
      id: 'POST_DISPLAY_CLEANUP', 
      order: 3, 
      icon: 'fas fa-desktop text-secondary',
      name: 'After Display Cleanup',
      description: 'Commands executed after display cleanup'
    },
    { 
      id: 'POST_STREAM_STOP', 
      order: 4, 
      icon: 'fas fa-stop-circle text-secondary',
      name: 'After Stream Stop',
      description: 'Commands executed after the stream has stopped'
    },
    { 
      id: 'ADDITIONAL_CLIENT_DISCONNECT', 
      order: 5, 
      icon: 'fas fa-user-slash text-danger',
      name: 'Additional Client Disconnect',
      description: 'Commands executed when an additional client disconnects'
    }
  ]

  // Computed properties
  const hasLegacyCommands = computed(() => {
    return props.legacyCommands && props.legacyCommands.length > 0
  })

  const commandsData = computed({
    get() {
      return props.modelValue || {}
    },
    set(value) {
      emit('update:modelValue', value)
    }
  })

  const hasAnyCommands = computed(() => {
    return Object.keys(commandsData.value).some(stage => 
      commandsData.value[stage] && commandsData.value[stage].length > 0
    ) || (props.legacyCommands && props.legacyCommands.length > 0)
  })

  // Detect if advanced commands exist (anything other than basic PRE_STREAM_START and POST_STREAM_STOP)
  const hasAdvancedCommands = computed(() => {
    const data = commandsData.value
    if (!data || Object.keys(data).length === 0) {
      return false
    }
    
    // If there are any commands at all in the advanced format, consider it advanced
    return Object.keys(data).some(stage => 
      data[stage] && data[stage].length > 0
    )
  })

  // Initialize advanced mode based on existing data
  onMounted(() => {
    // Only switch to advanced mode if there are truly advanced commands 
    // (not just auto-migrated legacy commands)
    // For Global tab, we want to show legacy commands in basic view
    if (hasAdvancedCommands.value && !hasLegacyCommands.value) {
      isAdvancedMode.value = true
    }
  })

  // Basic mode - simplified DO/UNDO interface
  const basicCommands = computed({
    get() {
      if (props.legacyCommands && props.legacyCommands.length > 0) {
        return props.legacyCommands.map(cmd => ({
          do: cmd.do || '',
          undo: cmd.undo || '',
          elevated: cmd.elevated || false
        }))
      }
      const setupGroups = getStageGroups('PRE_STREAM_START')
      const cleanupGroups = getStageGroups('POST_STREAM_STOP')
      const commands = []
      const maxCommands = Math.max(
        setupGroups.reduce((total, group) => total + (group.commands?.length || 0), 0),
        cleanupGroups.reduce((total, group) => total + (group.commands?.length || 0), 0)
      )
      if (maxCommands === 0) {
        return []
      }
      let setupIndex = 0
      let cleanupIndex = 0
      for (let i = 0; i < maxCommands; i++) {
        const command = {
          do: '',
          undo: '',
          elevated: false
        }
        
        // Find the setup command at the current index
        let currentSetupIndex = setupIndex
        for (const group of setupGroups) {
          const groupSize = group.commands?.length || 0
          if (currentSetupIndex < groupSize) {
            const setupCmd = group.commands[currentSetupIndex]
            command.do = setupCmd.cmd || ''
            command.elevated = setupCmd.elevated || false
            break
          }
          currentSetupIndex -= groupSize
        }
        setupIndex++
        
        // Find the cleanup command at the current index
        let currentCleanupIndex = cleanupIndex
        for (const group of cleanupGroups) {
          const groupSize = group.commands?.length || 0
          if (currentCleanupIndex < groupSize) {
            const cleanupCmd = group.commands[currentCleanupIndex]
            command.undo = cleanupCmd.cmd || ''
            break
          }
          currentCleanupIndex -= groupSize
        }
        cleanupIndex++
        
        commands.push(command)
      }
      return commands
    },
    set(newCommands) {
      console.log('basicCommands setter called with:', newCommands)
      const setupGroup = {
        name: 'Application Setup Commands',
        failure_policy: 'FAIL_FAST',
        commands: []
      }
      const cleanupGroup = {
        name: 'Application Cleanup Commands',
        failure_policy: 'CONTINUE_ON_FAILURE',
        commands: []
      }
      // Add setup commands in order
      newCommands.forEach((command, index) => {
        console.log(`Processing setup command ${index}:`, command.do)
        if (command.do?.trim()) {
          setupGroup.commands.push({
            cmd: command.do.trim(),
            elevated: command.elevated || false,
            timeout_seconds: 30,
            ignore_error: false,
            async: false
          })
        } else {
          setupGroup.commands.push({ 
            cmd: '', 
            elevated: false, 
            timeout_seconds: 30,
            ignore_error: false,
            async: false
          })
        }
      })
      console.log('Setup commands final:', setupGroup.commands)
      
      // Add cleanup (undo) commands in reverse order
      console.log('Processing cleanup commands in reverse order:')
      for (let i = newCommands.length - 1; i >= 0; i--) {
        const command = newCommands[i];
        console.log(`Processing cleanup command ${i}:`, command.undo)
        if (command.undo?.trim()) {
          cleanupGroup.commands.push({
            cmd: command.undo.trim(),
            elevated: command.elevated || false,
            timeout_seconds: 30,
            ignore_error: false,
            async: false
          })
        } else {
          cleanupGroup.commands.push({ 
            cmd: '', 
            elevated: false, 
            timeout_seconds: 30,
            ignore_error: false,
            async: false
          })
        }
      }
      console.log('Cleanup commands final:', cleanupGroup.commands)
      
      const updated = { ...commandsData.value }
      updated.PRE_STREAM_START = [setupGroup]
      updated.POST_STREAM_STOP = [cleanupGroup]
      console.log('About to set commandsData to:', updated)
      commandsData.value = updated
    }
  })

  function getStageGroups(stageId) {
    return commandsData.value[stageId] || []
  }

  function updateStageGroups(stageId, groups) {
    const updated = { ...commandsData.value }
    if (groups && groups.length > 0) {
      updated[stageId] = groups
    } else {
      delete updated[stageId]
    }
    commandsData.value = updated
  }

  function getAllStartStageGroups() {
    const allGroups = []
    startStages.forEach(stage => {
      const groups = getStageGroups(stage.id)
      groups.forEach(group => {
        allGroups.push({
          ...group,
          stage: stage.id,
          stageName: stage.name,
          stageDescription: stage.description,
          stageIcon: stage.icon
        })
      })
    })
    return allGroups
  }

  function getAllCleanupStageGroups() {
    const allGroups = []
    cleanupStages.forEach(stage => {
      const groups = getStageGroups(stage.id)
      groups.forEach(group => {
        allGroups.push({
          ...group,
          stage: stage.id,
          stageName: stage.name,
          stageDescription: stage.description,
          stageIcon: stage.icon
        })
      })
    })
    return allGroups
  }

  function updateAllStartStageGroups(newGroups) {
    const updated = { ...commandsData.value }
    startStages.forEach(stage => {
      delete updated[stage.id]
    })
    const groupsByStage = {}
    newGroups.forEach(group => {
      if (!groupsByStage[group.stage]) {
        groupsByStage[group.stage] = []
      }
      const cleanGroup = { ...group }
      delete cleanGroup.stage
      delete cleanGroup.stageName
      delete cleanGroup.stageDescription
      delete cleanGroup.stageIcon
      groupsByStage[group.stage].push(cleanGroup)
    })
    Object.keys(groupsByStage).forEach(stageId => {
      updated[stageId] = groupsByStage[stageId]
    })
    commandsData.value = updated
  }

  function updateAllCleanupStageGroups(newGroups) {
    const updated = { ...commandsData.value }
    cleanupStages.forEach(stage => {
      delete updated[stage.id]
    })
    const groupsByStage = {}
    newGroups.forEach(group => {
      if (!groupsByStage[group.stage]) {
        groupsByStage[group.stage] = []
      }
      const cleanGroup = { ...group }
      delete cleanGroup.stage
      delete cleanGroup.stageName
      delete cleanGroup.stageDescription
      delete cleanGroup.stageIcon
      groupsByStage[group.stage].push(cleanGroup)
    })
    Object.keys(groupsByStage).forEach(stageId => {
      updated[stageId] = groupsByStage[stageId]
    })
    commandsData.value = updated
  }

  function migrateLegacyToBasicCommands() {
    if (props.legacyCommands && props.legacyCommands.length > 0) {
      const setupGroup = {
        name: 'Application Setup Commands',
        failure_policy: 'FAIL_FAST',
        commands: []
      }
      const cleanupGroup = {
        name: 'Application Cleanup Commands',
        failure_policy: 'CONTINUE_ON_FAILURE',
        commands: []
      }
      
      // Process setup commands in order
      props.legacyCommands.forEach(cmd => {
        setupGroup.commands.push({
          cmd: cmd.do ? cmd.do.trim() : '',
          elevated: cmd.elevated || false,
          timeout_seconds: 30
        })
      })
      
      // Process cleanup commands in reverse order (undo should happen in reverse)
      for (let i = props.legacyCommands.length - 1; i >= 0; i--) {
        const cmd = props.legacyCommands[i]
        cleanupGroup.commands.push({
          cmd: cmd.undo ? cmd.undo.trim() : '',
          elevated: cmd.elevated || false,
          timeout_seconds: 30
        })
      }
      
      const updated = { ...commandsData.value }
      updated.PRE_STREAM_START = [setupGroup]
      updated.POST_STREAM_STOP = [cleanupGroup]
      commandsData.value = updated
      props.legacyCommands.length = 0
    }
  }


  function addBasicCommand() {
    const newCommand = {
      do: '',
      undo: '',
      elevated: false
    }
    const updated = [...basicCommands.value, newCommand]
    basicCommands.value = updated
  }

  function removeBasicCommand(index) {
    if (confirm('Are you sure you want to remove this command?')) {
      const updated = [...basicCommands.value]
      updated.splice(index, 1)
      basicCommands.value = updated
    }
  }

  function updateBasicCommand(index, field, value) {
    const updated = [...basicCommands.value]
    updated[index][field] = value
    basicCommands.value = updated
  }

  function moveBasicCommand(index, direction) {
    const updated = [...basicCommands.value]
    const newIndex = index + direction
    if (newIndex >= 0 && newIndex < updated.length) {
      [updated[index], updated[newIndex]] = [updated[newIndex], updated[index]]
      basicCommands.value = updated
    }
  }

  function getCommandPlaceholder() {
    if (props.platform === 'windows') {
      return 'e.g., taskkill /f /im "notepad.exe"'
    } else {
      return 'e.g., pkill firefox'
    }
  }

  function getUndoPlaceholder() {
    if (props.platform === 'windows') {
      return 'e.g., start notepad.exe'
    } else {
      return 'e.g., firefox &'
    }
  }

  // New methods for advanced/basic mode switching
  function switchToAdvancedMode() {
    // Convert basic commands to advanced format if they exist
    if (basicCommands.value && basicCommands.value.length > 0) {
      console.log('switchToAdvancedMode: Converting commands:', basicCommands.value)
      // Force conversion by setting the basicCommands value to itself
      // This will trigger the setter which properly handles reverse order for undo commands
      const currentCommands = [...basicCommands.value]
      console.log('switchToAdvancedMode: About to set basicCommands with:', currentCommands)
      basicCommands.value = currentCommands
      console.log('switchToAdvancedMode: After setting, commandsData is:', commandsData.value)
    }
    isAdvancedMode.value = true
    showAdvancedModal.value = true
  }

  function switchToBasicMode() {
    // Don't delete existing advanced commands, just switch back to basic view
    isAdvancedMode.value = false
    showAdvancedModal.value = false
  }

  return {
    isAdvancedMode,
    showAdvancedModal,
    startStages,
    cleanupStages,
    hasLegacyCommands,
    commandsData,
    hasAnyCommands,
    hasAdvancedCommands,
    basicCommands,
    getStageGroups,
    updateStageGroups,
    getAllStartStageGroups,
    getAllCleanupStageGroups,
    updateAllStartStageGroups,
    updateAllCleanupStageGroups,
    addBasicCommand,
    removeBasicCommand,
    updateBasicCommand,
    moveBasicCommand,
    getCommandPlaceholder,
    getUndoPlaceholder,
    switchToAdvancedMode,
    switchToBasicMode
  }
}
