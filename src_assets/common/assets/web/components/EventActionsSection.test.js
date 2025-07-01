import { describe, it, expect, beforeEach } from 'vitest'
import { mount } from '@vue/test-utils'
import EventActionsSection from './EventActionsSection.vue'

describe('EventActionsSection - Pair Command Logic', () => {
  let wrapper
  let component

  // Test data based on your actual configuration
  const testData = {
    "POST_STREAM_STOP": [{
      "commands": [
        {
          "async": false,
          "cmd": "powershell.exe -executionpolicy bypass -windowstyle hidden -file \"D:\\sources\\PlayNite Watcher\\PlayNiteWatcher-EndScript.ps1\" True",
          "elevated": false,
          "ignore_error": false,
          "timeout_seconds": 30
        },
        {
          "async": false,
          "cmd": "powershell.exe -executionpolicy bypass -file \"D:\\sources\\RTSSLimiter\\Helpers.ps1\" -n RTSSLimiter -t 1",
          "elevated": true,
          "ignore_error": false,
          "timeout_seconds": 30
        },
        {
          "async": false,
          "cmd": "powershell.exe -executionpolicy bypass -file \"D:\\sources\\AutoHDRSwitch\\UndoScript.ps1\" -n AutoHDR",
          "elevated": false,
          "ignore_error": false,
          "timeout_seconds": 30
        },
        {
          "async": false,
          "cmd": "powershell.exe -executionpolicy bypass -file \"D:\\sources\\ResolutionAutomation\\UndoScript.ps1\" -n ResolutionMatcher",
          "elevated": false,
          "ignore_error": false,
          "timeout_seconds": 30
        },
        {
          "async": false,
          "cmd": "powershell.exe -executionpolicy bypass -file \"D:\\sources\\MonitorSwapAutomation\\UndoScript.ps1\" -n MonitorSwapper",
          "elevated": false,
          "ignore_error": false,
          "timeout_seconds": 30
        },
        {
          "async": false,
          "cmd": "powershell.exe -executionpolicy bypass -file \"D:\\sources\\AudioSwapper\\AudioSwapper-Functions.ps1\" True",
          "elevated": false,
          "ignore_error": false,
          "timeout_seconds": 30
        }
      ],
      "failure_policy": "CONTINUE_ON_FAILURE",
      "name": "Application Cleanup Commands"
    }],
    "PRE_STREAM_START": [{
      "commands": [
        {
          "async": false,
          "cmd": "powershell.exe -executionpolicy bypass -file \"D:\\sources\\AudioSwapper\\AudioSwapper.ps1\"",
          "elevated": false,
          "ignore_error": false,
          "timeout_seconds": 30
        },
        {
          "async": false,
          "cmd": "powershell.exe -executionpolicy bypass -file \"D:\\sources\\MonitorSwapAutomation\\StreamMonitor.ps1\" -n MonitorSwapper",
          "elevated": false,
          "ignore_error": false,
          "timeout_seconds": 30
        },
        {
          "async": false,
          "cmd": "powershell.exe -executionpolicy bypass -file \"D:\\sources\\ResolutionAutomation\\StreamMonitor.ps1\" -n ResolutionMatcher",
          "elevated": false,
          "ignore_error": false,
          "timeout_seconds": 30
        },
        {
          "async": false,
          "cmd": "powershell.exe -executionpolicy bypass -file \"D:\\sources\\AutoHDRSwitch\\StreamMonitor.ps1\" -n AutoHDR",
          "elevated": false,
          "ignore_error": false,
          "timeout_seconds": 30
        },
        {
          "async": false,
          "cmd": "powershell.exe -executionpolicy bypass -file \"D:\\sources\\RTSSLimiter\\StreamMonitor.ps1\" -n RTSSLimiter",
          "elevated": true,
          "ignore_error": false,
          "timeout_seconds": 30
        },
        {
          "async": false,
          "cmd": "",
          "elevated": false,
          "ignore_error": false,
          "timeout_seconds": 30
        }
      ],
      "failure_policy": "FAIL_FAST",
      "name": "Application Setup Commands"
    }]
  }

  beforeEach(() => {
    wrapper = mount(EventActionsSection, {
      props: {
        modelValue: testData,
        platform: 'windows'
      }
    })
    component = wrapper.vm
  })

  describe('Semantic Pairings Based on Original global_prep_cmd', () => {
    // Based on original global_prep_cmd structure:
    // [0] AudioSwapper.ps1 → AudioSwapper-Functions.ps1
    // [1] MonitorSwapper StreamMonitor.ps1 → MonitorSwapper UndoScript.ps1
    // [2] ResolutionMatcher StreamMonitor.ps1 → ResolutionMatcher UndoScript.ps1  
    // [3] AutoHDR StreamMonitor.ps1 → AutoHDR UndoScript.ps1
    // [4] RTSSLimiter StreamMonitor.ps1 → RTSSLimiter Helpers.ps1
    // [5] (empty) → PlayNiteWatcher-EndScript.ps1

    it('should pair AudioSwapper setup with AudioSwapper cleanup (semantic match)', () => {
      const setupCommand = {
        ...testData.PRE_STREAM_START[0].commands[0],
        stage: 'PRE_STREAM_START'
      }

      const pairCommand = component.findPairCommand(setupCommand)

      expect(pairCommand).toBeTruthy()
      expect(pairCommand.stage).toBe('POST_STREAM_STOP')

      // Verify semantic matching: both should contain "AudioSwapper"
      expect(setupCommand.cmd).toContain('AudioSwapper')
      expect(pairCommand.cmd).toContain('AudioSwapper')

      // Verify the specific cleanup command
      expect(pairCommand.cmd).toBe("powershell.exe -executionpolicy bypass -file \"D:\\sources\\AudioSwapper\\AudioSwapper-Functions.ps1\" True")
    })

    it('should pair MonitorSwapper setup with MonitorSwapper cleanup (semantic match)', () => {
      const setupCommand = {
        ...testData.PRE_STREAM_START[0].commands[1],
        stage: 'PRE_STREAM_START'
      }

      const pairCommand = component.findPairCommand(setupCommand)

      expect(pairCommand).toBeTruthy()
      expect(pairCommand.stage).toBe('POST_STREAM_STOP')

      // Verify semantic matching: both should contain "MonitorSwapper"
      expect(setupCommand.cmd).toContain('MonitorSwapper')
      expect(pairCommand.cmd).toContain('MonitorSwapper')

      // Verify the specific cleanup command
      expect(pairCommand.cmd).toBe("powershell.exe -executionpolicy bypass -file \"D:\\sources\\MonitorSwapAutomation\\UndoScript.ps1\" -n MonitorSwapper")
    })

    it('should pair ResolutionMatcher setup with ResolutionMatcher cleanup (semantic match)', () => {
      const setupCommand = {
        ...testData.PRE_STREAM_START[0].commands[2],
        stage: 'PRE_STREAM_START'
      }

      const pairCommand = component.findPairCommand(setupCommand)

      expect(pairCommand).toBeTruthy()
      expect(pairCommand.stage).toBe('POST_STREAM_STOP')

      // Verify semantic matching: both should contain "ResolutionMatcher"
      expect(setupCommand.cmd).toContain('ResolutionMatcher')
      expect(pairCommand.cmd).toContain('ResolutionMatcher')

      // Verify the specific cleanup command
      expect(pairCommand.cmd).toBe("powershell.exe -executionpolicy bypass -file \"D:\\sources\\ResolutionAutomation\\UndoScript.ps1\" -n ResolutionMatcher")
    })

    it('should pair AutoHDR setup with AutoHDR cleanup (semantic match)', () => {
      const setupCommand = {
        ...testData.PRE_STREAM_START[0].commands[3],
        stage: 'PRE_STREAM_START'
      }

      const pairCommand = component.findPairCommand(setupCommand)

      expect(pairCommand).toBeTruthy()
      expect(pairCommand.stage).toBe('POST_STREAM_STOP')

      // Verify semantic matching: both should contain "AutoHDR"
      expect(setupCommand.cmd).toContain('AutoHDR')
      expect(pairCommand.cmd).toContain('AutoHDR')

      // Verify the specific cleanup command
      expect(pairCommand.cmd).toBe("powershell.exe -executionpolicy bypass -file \"D:\\sources\\AutoHDRSwitch\\UndoScript.ps1\" -n AutoHDR")
    })

    it('should pair RTSSLimiter setup with RTSSLimiter cleanup (semantic match)', () => {
      const setupCommand = {
        ...testData.PRE_STREAM_START[0].commands[4],
        stage: 'PRE_STREAM_START'
      }

      const pairCommand = component.findPairCommand(setupCommand)

      expect(pairCommand).toBeTruthy()
      expect(pairCommand.stage).toBe('POST_STREAM_STOP')

      // Verify semantic matching: both should contain "RTSSLimiter"
      expect(setupCommand.cmd).toContain('RTSSLimiter')
      expect(pairCommand.cmd).toContain('RTSSLimiter')

      // Verify the specific cleanup command
      expect(pairCommand.cmd).toBe("powershell.exe -executionpolicy bypass -file \"D:\\sources\\RTSSLimiter\\Helpers.ps1\" -n RTSSLimiter -t 1")
    })

    it('should pair empty setup command with PlayNiteWatcher cleanup (semantic match)', () => {
      const setupCommand = {
        ...testData.PRE_STREAM_START[0].commands[5],
        stage: 'PRE_STREAM_START'
      }

      const pairCommand = component.findPairCommand(setupCommand)

      expect(pairCommand).toBeTruthy()
      expect(pairCommand.stage).toBe('POST_STREAM_STOP')

      // Verify that empty setup pairs with PlayNiteWatcher cleanup (as per original config)
      expect(setupCommand.cmd).toBe("")
      expect(pairCommand.cmd).toContain('PlayNite')

      // Verify the specific cleanup command
      expect(pairCommand.cmd).toBe("powershell.exe -executionpolicy bypass -windowstyle hidden -file \"D:\\sources\\PlayNite Watcher\\PlayNiteWatcher-EndScript.ps1\" True")
    })
  })

  describe('Direct Array Reverse Test', () => {
    it('should confirm that setup commands pair with reversed cleanup commands', () => {
      const setupCommands = testData.PRE_STREAM_START[0].commands
      const cleanupCommands = testData.POST_STREAM_STOP[0].commands

      // Test each setup command pairs with its corresponding reversed cleanup command
      setupCommands.forEach((setupCmd, index) => {
        const setupCommand = {
          ...setupCmd,
          stage: 'PRE_STREAM_START'
        }

        const pairCommand = component.findPairCommand(setupCommand)
        const expectedCleanupIndex = cleanupCommands.length - 1 - index
        const expectedCleanupCommand = cleanupCommands[expectedCleanupIndex]

        expect(pairCommand).toBeTruthy()
        expect(pairCommand.cmd).toBe(expectedCleanupCommand.cmd)
        expect(pairCommand.commandIndex).toBe(expectedCleanupIndex)
      })
    })

    it('should confirm that cleanup commands pair with reversed setup commands', () => {
      const setupCommands = testData.PRE_STREAM_START[0].commands
      const cleanupCommands = testData.POST_STREAM_STOP[0].commands

      // Test each cleanup command pairs with its corresponding reversed setup command
      cleanupCommands.forEach((cleanupCmd, index) => {
        const cleanupCommand = {
          ...cleanupCmd,
          stage: 'POST_STREAM_STOP'
        }

        const pairCommand = component.findPairCommand(cleanupCommand)
        const expectedSetupIndex = setupCommands.length - 1 - index
        const expectedSetupCommand = setupCommands[expectedSetupIndex]

        expect(pairCommand).toBeTruthy()
        expect(pairCommand.cmd).toBe(expectedSetupCommand.cmd)
        expect(pairCommand.commandIndex).toBe(expectedSetupIndex)
      })
    })
  })

  describe('Reverse Pairings (Cleanup to Setup)', () => {
    it('should pair AudioSwapper cleanup (POST_STREAM_STOP[5]) with AudioSwapper setup (PRE_STREAM_START[0])', () => {
      const cleanupCommand = {
        ...testData.POST_STREAM_STOP[0].commands[5],
        stage: 'POST_STREAM_STOP'
      }

      const pairCommand = component.findPairCommand(cleanupCommand)

      expect(pairCommand).toBeTruthy()
      expect(pairCommand.cmd).toBe("powershell.exe -executionpolicy bypass -file \"D:\\sources\\AudioSwapper\\AudioSwapper.ps1\"")
      expect(pairCommand.stage).toBe('PRE_STREAM_START')
      expect(pairCommand.commandIndex).toBe(0) // First command in setup
    })

    it('should pair MonitorSwapper cleanup (POST_STREAM_STOP[4]) with MonitorSwapper setup (PRE_STREAM_START[1])', () => {
      const cleanupCommand = {
        ...testData.POST_STREAM_STOP[0].commands[4],
        stage: 'POST_STREAM_STOP'
      }

      const pairCommand = component.findPairCommand(cleanupCommand)

      expect(pairCommand).toBeTruthy()
      expect(pairCommand.cmd).toBe("powershell.exe -executionpolicy bypass -file \"D:\\sources\\MonitorSwapAutomation\\StreamMonitor.ps1\" -n MonitorSwapper")
      expect(pairCommand.stage).toBe('PRE_STREAM_START')
      expect(pairCommand.commandIndex).toBe(1) // Second command in setup
    })

    it('should pair PlayNiteWatcher cleanup (POST_STREAM_STOP[0]) with empty setup command (PRE_STREAM_START[5])', () => {
      const cleanupCommand = {
        ...testData.POST_STREAM_STOP[0].commands[0],
        stage: 'POST_STREAM_STOP'
      }

      const pairCommand = component.findPairCommand(cleanupCommand)

      expect(pairCommand).toBeTruthy()
      expect(pairCommand.cmd).toBe("")
      expect(pairCommand.stage).toBe('PRE_STREAM_START')
      expect(pairCommand.commandIndex).toBe(5) // Last command in setup
    })
  })

  describe('Edge Cases', () => {
    it('should return null for commands with no paired stage', () => {
      const command = {
        cmd: "test command",
        stage: 'STREAM_RESUME', // No pair for this stage
        timeout_seconds: 30,
        elevated: false,
        ignore_error: false,
        async: false
      }

      const pairCommand = component.findPairCommand(command)
      expect(pairCommand).toBeNull()
    })

    it('should return null for commands that cannot be found in their own stage', () => {
      const command = {
        cmd: "non-existent command",
        stage: 'PRE_STREAM_START',
        timeout_seconds: 30,
        elevated: false,
        ignore_error: false,
        async: false
      }

      const pairCommand = component.findPairCommand(command)
      expect(pairCommand).toBeNull()
    })

    it('should handle index out of bounds gracefully', () => {
      // Create a test case where the target stage has fewer commands than expected
      const limitedData = {
        "PRE_STREAM_START": [{
          "commands": [
            {
              "async": false,
              "cmd": "test command",
              "elevated": false,
              "ignore_error": false,
              "timeout_seconds": 30
            }
          ]
        }],
        "POST_STREAM_STOP": [{
          "commands": [] // Empty cleanup commands
        }]
      }

      const limitedWrapper = mount(EventActionsSection, {
        props: {
          modelValue: limitedData,
          platform: 'windows'
        }
      })

      const command = {
        ...limitedData.PRE_STREAM_START[0].commands[0],
        stage: 'PRE_STREAM_START'
      }

      const pairCommand = limitedWrapper.vm.findPairCommand(command)
      expect(pairCommand).toBeNull()
    })
  })

  describe('Index Calculation Logic', () => {
    it('should use simple reverse index calculation: setup[i] ↔ cleanup[length-1-i]', () => {
      const setupCommands = testData.PRE_STREAM_START[0].commands
      const cleanupCommands = testData.POST_STREAM_STOP[0].commands

      expect(setupCommands.length).toBe(cleanupCommands.length) // Should be equal arrays

      setupCommands.forEach((setupCmd, i) => {
        const setupCommand = {
          ...setupCmd,
          stage: 'PRE_STREAM_START'
        }

        const pairCommand = component.findPairCommand(setupCommand)
        const expectedCleanupIndex = cleanupCommands.length - 1 - i

        expect(pairCommand).toBeTruthy()
        expect(pairCommand.commandIndex).toBe(expectedCleanupIndex)
        expect(pairCommand.cmd).toBe(cleanupCommands[expectedCleanupIndex].cmd)
      })
    })
  })

  describe('Command Deletion', () => {
    it('deleting a main command also deletes its paired command', async () => {
      // setupA (PRE_STREAM_START[0]) pairs with cleanupA (POST_STREAM_STOP[1])
      window.confirm = () => true
      // Remove setupA (index 0)
      await component.removeCommand(0)
      // Should remove both setupA and cleanupA
      const data = wrapper.emitted()['update:modelValue'].at(-1)[0]
      expect(data.PRE_STREAM_START[0].commands.length).toBe(1)
      expect(data.PRE_STREAM_START[0].commands[0].cmd).toBe('setupB')
      expect(data.POST_STREAM_STOP[0].commands.length).toBe(1)
      expect(data.POST_STREAM_STOP[0].commands[0].cmd).toBe('cleanupB')
    })

    it('deleting a paired command also deletes its main command', async () => {
      // cleanupA (POST_STREAM_STOP[1]) pairs with setupA (PRE_STREAM_START[0])
      window.confirm = () => true
      // Remove cleanupA (index 3: 2 setup + 2 cleanup, so index 3 is POST_STREAM_STOP[1])
      await component.removeCommand(3)
      // Should remove both cleanupA and setupA
      const data = wrapper.emitted()['update:modelValue'].at(-1)[0]
      expect(data.PRE_STREAM_START[0].commands.length).toBe(1)
      expect(data.PRE_STREAM_START[0].commands[0].cmd).toBe('setupB')
      expect(data.POST_STREAM_STOP[0].commands.length).toBe(1)
      expect(data.POST_STREAM_STOP[0].commands[0].cmd).toBe('cleanupB')
    })

    it('deleting a command with no pair only deletes itself', async () => {
      // Remove setupB (PRE_STREAM_START[1])
      window.confirm = () => true
      await component.removeCommand(1)
      const data = wrapper.emitted()['update:modelValue'].at(-1)[0]
      expect(data.PRE_STREAM_START[0].commands.length).toBe(1)
      expect(data.PRE_STREAM_START[0].commands[0].cmd).toBe('setupA')
      expect(data.POST_STREAM_STOP[0].commands.length).toBe(2)
    })

    it('deleting when pair is already missing does not error', async () => {
      // Remove setupA and its pair first
      window.confirm = () => true
      await component.removeCommand(0)
      // Now remove setupA again (should do nothing or not error)
      await component.removeCommand(0)
      const data = wrapper.emitted()['update:modelValue'].at(-1)[0]
      expect(data.PRE_STREAM_START[0].commands.length).toBe(1)
      expect(data.PRE_STREAM_START[0].commands[0].cmd).toBe('setupB')
      expect(data.POST_STREAM_STOP[0].commands.length).toBe(1)
      expect(data.POST_STREAM_STOP[0].commands[0].cmd).toBe('cleanupB')
    })
  });
})
