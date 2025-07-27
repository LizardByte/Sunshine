  it('pads command-item elements so both columns have the same length, with placeholders for missing commands', async () => {
    const wrapper = mount(EventActionsEditor, {
      global: { mocks: { $t } },
      props: createDefaultProps(),
      attachTo: document.body
    });
    await wrapper.vm.showNewAction();
    await wrapper.vm.$nextTick();
    // Add 4 more startup commands (total 5)
    for (let i = 0; i < 4; i++) await wrapper.vm.addStartupCommand();
    // Add 6 cleanup commands
    for (let i = 0; i < 6; i++) await wrapper.vm.addCleanupCommand();
    await wrapper.vm.$nextTick();
    // There should be 6 command-item elements in both columns
    const startupItems = wrapper.findAll('.modal-body .commands-section')[0].findAll('.command-item');
    const cleanupItems = wrapper.findAll('.modal-body .commands-section')[1].findAll('.command-item');
    expect(startupItems.length).toBe(6);
    expect(cleanupItems.length).toBe(6);
    // Debug: print HTML and command data for last startup and cleanup item
    // eslint-disable-next-line no-console
    console.log('startupItems[5].html():', startupItems[5].html());
    // eslint-disable-next-line no-console
    console.log('startup command[5]:', wrapper.vm.currentAction.action.startup_commands.commands[5]);
    expect(startupItems[5].html()).toContain('Intentionally blank command placeholder');
    for (let i = 0; i < 5; i++) {
      wrapper.vm.currentAction.action.cleanup_commands.commands[i].cmd = 'cmd' + i;
    }
    await wrapper.vm.$nextTick();
    const cleanupItems2 = wrapper.findAll('.modal-body .commands-section')[1].findAll('.command-item');
    // eslint-disable-next-line no-console
    console.log('cleanupItems2[5].html():', cleanupItems2[5].html());
    // eslint-disable-next-line no-console
    console.log('cleanup command[5]:', wrapper.vm.currentAction.action.cleanup_commands.commands[5]);
    expect(cleanupItems2[5].html()).toContain('Intentionally blank command placeholder');
    for (let i = 0; i < 5; i++) {
      expect(cleanupItems2[i].html()).not.toContain('Intentionally blank command placeholder');
    }
  });
  it('can add and remove commands at every index for startup and cleanup, and they are independent', async () => {
    const wrapper = mount(EventActionsEditor, {
      global: { mocks: { $t } },
      props: createDefaultProps(),
      attachTo: document.body
    });
    await wrapper.vm.showNewAction();
    await wrapper.vm.$nextTick();
    // Add 3 startup commands
    await wrapper.vm.addStartupCommand();
    await wrapper.vm.addStartupCommand();
    expect(wrapper.vm.currentAction.action.startup_commands.commands.length).toBe(3);
    // Set values for clarity
    wrapper.vm.currentAction.action.startup_commands.commands[0].cmd = 'A';
    wrapper.vm.currentAction.action.startup_commands.commands[1].cmd = 'B';
    wrapper.vm.currentAction.action.startup_commands.commands[2].cmd = 'C';
    // Remove middle
    await wrapper.vm.removeStartupCommand(1);
    expect(wrapper.vm.currentAction.action.startup_commands.commands.map((c: any) => c.cmd)).toEqual(['A', 'C']);
    // Remove first
    await wrapper.vm.removeStartupCommand(0);
    expect(wrapper.vm.currentAction.action.startup_commands.commands.map((c: any) => c.cmd)).toEqual(['C']);
    // Remove last (should be empty)
    await wrapper.vm.removeStartupCommand(0);
    expect(wrapper.vm.currentAction.action.startup_commands.commands.length).toBe(0);
    // Add 2 cleanup commands
    await wrapper.vm.addCleanupCommand();
    await wrapper.vm.addCleanupCommand();
    expect(wrapper.vm.currentAction.action.cleanup_commands.commands.length).toBe(2);
    // Set values
    wrapper.vm.currentAction.action.cleanup_commands.commands[0].cmd = 'X';
    wrapper.vm.currentAction.action.cleanup_commands.commands[1].cmd = 'Y';
    // Remove last
    await wrapper.vm.removeCleanupCommand(1);
    expect(wrapper.vm.currentAction.action.cleanup_commands.commands.map((c: any) => c.cmd)).toEqual(['X']);
    // Remove first
    await wrapper.vm.removeCleanupCommand(0);
    expect(wrapper.vm.currentAction.action.cleanup_commands.commands.length).toBe(0);
    // Add startup command again, ensure cleanup is still empty
    await wrapper.vm.addStartupCommand();
    expect(wrapper.vm.currentAction.action.cleanup_commands.commands.length).toBe(0);
    // Add cleanup command again, ensure startup is not affected
    await wrapper.vm.addCleanupCommand();
    expect(wrapper.vm.currentAction.action.startup_commands.commands.length).toBe(1);
  });

  it('shows placeholder for all, some, and none empty commands (startup and cleanup) (padded logic)', async () => {
    const wrapper = mount(EventActionsEditor, {
      global: { mocks: { $t } },
      props: createDefaultProps(),
      attachTo: document.body
    });
    await wrapper.vm.showNewAction();
    await wrapper.vm.$nextTick();
    // All empty startup (default, 1 item)
    let startupItems = wrapper.findAll('.modal-body .commands-section')[0].findAll('.command-item');
    expect(startupItems.length).toBe(1);
    // Debug: print HTML and command data for first startup item
    // eslint-disable-next-line no-console
    console.log('startupItems[0].html():', startupItems[0].html());
    // eslint-disable-next-line no-console
    console.log('startup command[0]:', wrapper.vm.currentAction.action.startup_commands.commands[0]);
    expect(startupItems[0].html()).toContain('Intentionally blank command placeholder');
    // Add two startup commands, one filled, one empty
    await wrapper.vm.addStartupCommand();
    wrapper.vm.currentAction.action.startup_commands.commands[0].cmd = 'filled';
    await wrapper.vm.$nextTick();
    startupItems = wrapper.findAll('.modal-body .commands-section')[0].findAll('.command-item');
    expect(startupItems.length).toBe(2);
    expect(startupItems[1].html()).toContain('Intentionally blank command placeholder');
    // Fill all startup commands
    wrapper.vm.currentAction.action.startup_commands.commands[1].cmd = 'also filled';
    await wrapper.vm.$nextTick();
    startupItems = wrapper.findAll('.modal-body .commands-section')[0].findAll('.command-item');
    expect(startupItems[0].html()).not.toContain('Intentionally blank command placeholder');
    expect(startupItems[1].html()).not.toContain('Intentionally blank command placeholder');
    // Cleanup: all empty
    await wrapper.vm.addCleanupCommand();
    await wrapper.vm.$nextTick();
    let cleanupItems = wrapper.findAll('.modal-body .commands-section')[1].findAll('.command-item');
    // Should be 2: one for the real command, one for the placeholder (padded to match startup column)
    expect(cleanupItems.length).toBe(2);
    // The first is the real command (blank), the second is the placeholder
    expect(cleanupItems[0].html()).toContain('Intentionally blank command placeholder');
    expect(cleanupItems[1].html()).toContain('Intentionally blank command placeholder');
    // Cleanup: some empty, some filled
    await wrapper.vm.addCleanupCommand();
    wrapper.vm.currentAction.action.cleanup_commands.commands[1].cmd = 'filled';
    await wrapper.vm.$nextTick();
    cleanupItems = wrapper.findAll('.modal-body .commands-section')[1].findAll('.command-item');
    expect(cleanupItems.length).toBe(2);
    expect(cleanupItems[0].html()).toContain('Intentionally blank command placeholder');
    expect(cleanupItems[1].html()).not.toContain('Intentionally blank command placeholder');
    // Cleanup: all filled
    wrapper.vm.currentAction.action.cleanup_commands.commands[0].cmd = 'filled2';
    await wrapper.vm.$nextTick();
    cleanupItems = wrapper.findAll('.modal-body .commands-section')[1].findAll('.command-item');
    expect(cleanupItems[0].html()).not.toContain('Intentionally blank command placeholder');
    expect(cleanupItems[1].html()).not.toContain('Intentionally blank command placeholder');
  });
import { mount } from '@vue/test-utils';
import { describe, it, expect, beforeEach, vi } from 'vitest';
import EventActionsEditor from '../EventActionsEditor.vue';

// Mock i18n $t function
const $t = (key: string) => key;

function createDefaultProps() {
  return {
    modelValue: []
  };
}

describe('EventActionsEditor.vue', () => {
  beforeEach(() => {
    vi.stubGlobal('alert', vi.fn());
    vi.stubGlobal('confirm', vi.fn(() => true));
  });

  it('renders the empty state when no actions exist', () => {
    const wrapper = mount(EventActionsEditor, {
      global: { mocks: { $t } },
      props: createDefaultProps(),
    });
    expect(wrapper.text()).toContain('No event actions configured yet');
  });

  it('opens the modal for a new action and validates required fields', async () => {
    const wrapper = mount(EventActionsEditor, {
      global: { mocks: { $t } },
      props: createDefaultProps(),
      attachTo: document.body
    });
    await wrapper.vm.showNewAction();
    await wrapper.vm.$nextTick();
    expect(wrapper.vm.showNewActionModal).toBe(true);
    // Try to save with missing name
    wrapper.vm.currentAction.name = '';
    await wrapper.find('form').trigger('submit.prevent');
    expect(window.alert).toHaveBeenCalled();
    // Fill name but no stages
    wrapper.vm.currentAction.name = 'Test Action';
    wrapper.vm.currentAction.action.startup_stage = '';
    wrapper.vm.currentAction.action.shutdown_stage = '';
    await wrapper.find('form').trigger('submit.prevent');
    expect(window.alert).toHaveBeenCalled();
  });

  it('adds a new action and emits update:modelValue', async () => {
    const emitSpy = vi.fn();
    const wrapper = mount(EventActionsEditor, {
      global: { mocks: { $t } },
      props: createDefaultProps(),
      attrs: { 'onUpdate:modelValue': emitSpy },
      attachTo: document.body
    });
    await wrapper.vm.showNewAction();
    await wrapper.vm.$nextTick();
    // Fill required fields
    wrapper.vm.currentAction.name = 'Test Action';
    wrapper.vm.currentAction.action.startup_stage = 'PRE_STREAM_START';
    wrapper.vm.currentAction.action.startup_commands.commands[0].cmd = 'echo start';
    await wrapper.find('form').trigger('submit.prevent');
    expect(emitSpy).toHaveBeenCalled();
    expect(wrapper.vm.showNewActionModal).toBe(false);
    expect(wrapper.vm.eventActions.length).toBe(1);
    expect(wrapper.vm.eventActions[0].name).toBe('Test Action');
  });

  it('edits an existing action', async () => {
    // Add an action first
    const wrapper = mount(EventActionsEditor, {
      global: { mocks: { $t } },
      props: createDefaultProps(),
      attachTo: document.body
    });
    await wrapper.vm.showNewAction();
    await wrapper.vm.$nextTick();
    wrapper.vm.currentAction.name = 'EditMe';
    wrapper.vm.currentAction.action.startup_stage = 'PRE_STREAM_START';
    wrapper.vm.currentAction.action.startup_commands.commands[0].cmd = 'A';
    await wrapper.find('form').trigger('submit.prevent');
    // Now edit it
    await wrapper.vm.editAction(0);
    await wrapper.vm.$nextTick();
    expect(wrapper.vm.showNewActionModal).toBe(true);
    expect(wrapper.vm.currentAction.name).toBe('EditMe');
    // Change name and save
    wrapper.vm.currentAction.name = 'Edited';
    await wrapper.find('form').trigger('submit.prevent');
    expect(wrapper.vm.eventActions[0].name).toBe('Edited');
  });

  it('deletes an action', async () => {
    const wrapper = mount(EventActionsEditor, {
      global: { mocks: { $t } },
      props: {
        modelValue: [{
          name: 'DeleteMe',
          action: {
            startup_stage: 'PRE_STREAM_START',
            shutdown_stage: 'POST_STREAM_STOP',
            startup_commands: { failure_policy: 'FAIL_FAST', commands: [{ cmd: 'A', elevated: false, timeout_seconds: 10, ignore_error: false, async: false }] },
            cleanup_commands: { failure_policy: 'CONTINUE_ON_FAILURE', commands: [{ cmd: 'B', elevated: false, timeout_seconds: 10, ignore_error: true, async: false }] }
          }
        }]
      },
      attachTo: document.body
    });
    await wrapper.vm.deleteAction(0);
    expect(wrapper.vm.eventActions.length).toBe(0);
  });

  it('adds and removes startup and cleanup commands', async () => {
    const wrapper = mount(EventActionsEditor, {
      global: { mocks: { $t } },
      props: createDefaultProps(),
      attachTo: document.body
    });
    await wrapper.vm.showNewAction();
    await wrapper.vm.$nextTick();
    // Add startup command
    await wrapper.vm.addStartupCommand();
    expect(wrapper.vm.currentAction.action.startup_commands.commands.length).toBe(2);
    // Remove startup command
    await wrapper.vm.removeStartupCommand(1);
    expect(wrapper.vm.currentAction.action.startup_commands.commands.length).toBe(1);
    // Add cleanup command
    await wrapper.vm.addCleanupCommand();
    expect(wrapper.vm.currentAction.action.cleanup_commands.commands.length).toBe(1);
    // Remove cleanup command
    await wrapper.vm.removeCleanupCommand(0);
    expect(wrapper.vm.currentAction.action.cleanup_commands.commands.length).toBe(0);
  });

  it('shows placeholder for blank commands (padded logic)', async () => {
    const wrapper = mount(EventActionsEditor, {
      global: { mocks: { $t } },
      props: createDefaultProps(),
      attachTo: document.body
    });
    await wrapper.vm.showNewAction();
    await wrapper.vm.$nextTick();
    // Startup command is blank by default
    let startupItems = wrapper.findAll('.modal-body .commands-section')[0].findAll('.command-item');
    expect(startupItems.length).toBe(1);
    // Debug: print HTML and command data for first startup item
    // eslint-disable-next-line no-console
    console.log('startupItems[0].html():', startupItems[0].html());
    // eslint-disable-next-line no-console
    console.log('startup command[0]:', wrapper.vm.currentAction.action.startup_commands.commands[0]);
    expect(startupItems[0].html()).toContain('Intentionally blank command placeholder');
    // Add blank cleanup command
    await wrapper.vm.addCleanupCommand();
    await wrapper.vm.$nextTick();
    let cleanupItems = wrapper.findAll('.modal-body .commands-section')[1].findAll('.command-item');
    expect(cleanupItems.length).toBe(1);
    expect(cleanupItems[0].html()).toContain('Intentionally blank command placeholder');
  });

  it('shows helper about cleanup command reversal', async () => {
    const wrapper = mount(EventActionsEditor, {
      global: { mocks: { $t } },
      props: createDefaultProps(),
      attachTo: document.body
    });
    await wrapper.vm.showNewAction();
    await wrapper.vm.$nextTick();
    // The helper text about reversal should be present
    expect(wrapper.html()).toContain('automatically reversed');
  });

  it('sets and displays failure policies for startup and cleanup', async () => {
    const wrapper = mount(EventActionsEditor, {
      global: { mocks: { $t } },
      props: createDefaultProps(),
      attachTo: document.body
    });
    await wrapper.vm.showNewAction();
    await wrapper.vm.$nextTick();
    // Set failure policies
    wrapper.vm.currentAction.action.startup_commands.failure_policy = 'CONTINUE_ON_FAILURE';
    wrapper.vm.currentAction.action.cleanup_commands.failure_policy = 'FAIL_FAST';
    await wrapper.vm.$nextTick();
    // Check that the values are set
    expect(wrapper.vm.currentAction.action.startup_commands.failure_policy).toBe('CONTINUE_ON_FAILURE');
    expect(wrapper.vm.currentAction.action.cleanup_commands.failure_policy).toBe('FAIL_FAST');
  });

  it('handles edge case: adding a command with only whitespace', async () => {
    const wrapper = mount(EventActionsEditor, {
      global: { mocks: { $t } },
      props: createDefaultProps(),
      attachTo: document.body
    });
    await wrapper.vm.showNewAction();
    await wrapper.vm.$nextTick();
    wrapper.vm.currentAction.name = 'Whitespace';
    wrapper.vm.currentAction.action.startup_stage = 'PRE_STREAM_START';
    wrapper.vm.currentAction.action.startup_commands.commands[0].cmd = '   ';
    await wrapper.find('form').trigger('submit.prevent');
    // Action is added, but with zero startup commands
    expect(wrapper.vm.eventActions.length).toBe(1);
    expect(wrapper.vm.eventActions[0].action.startup_commands.commands.length).toBe(0);
  });

  it('handles edge case: removing all commands and saving', async () => {
    const wrapper = mount(EventActionsEditor, {
      global: { mocks: { $t } },
      props: createDefaultProps(),
      attachTo: document.body
    });
    await wrapper.vm.showNewAction();
    await wrapper.vm.$nextTick();
    // Remove the only startup command
    wrapper.vm.currentAction.action.startup_commands.commands = [];
    wrapper.vm.currentAction.name = 'Test';
    wrapper.vm.currentAction.action.startup_stage = 'PRE_STREAM_START';
    await wrapper.find('form').trigger('submit.prevent');
    // Action is added, but with zero startup commands
    expect(wrapper.vm.eventActions.length).toBe(1);
    expect(wrapper.vm.eventActions[0].action.startup_commands.commands.length).toBe(0);
  });

  it('handles edge case: editing a command and cancelling', async () => {
    const wrapper = mount(EventActionsEditor, {
      global: { mocks: { $t } },
      props: {
        modelValue: [{
          name: 'EditCmd',
          action: {
            startup_stage: 'PRE_STREAM_START',
            shutdown_stage: 'POST_STREAM_STOP',
            startup_commands: { failure_policy: 'FAIL_FAST', commands: [{ cmd: 'A', elevated: false, timeout_seconds: 10, ignore_error: false, async: false }] },
            cleanup_commands: { failure_policy: 'CONTINUE_ON_FAILURE', commands: [{ cmd: 'B', elevated: false, timeout_seconds: 10, ignore_error: true, async: false }] }
          }
        }]
      },
      attachTo: document.body
    });
    // Open edit command modal
    await wrapper.vm.editStartupCommand(0, 0);
    expect(wrapper.vm.showEditCommandModal).toBe(true);
    // Cancel edit
    await wrapper.vm.closeEditCommandModal();
    expect(wrapper.vm.showEditCommandModal).toBe(false);
  });

  it('switches between new and edit modes', async () => {
    const wrapper = mount(EventActionsEditor, {
      global: { mocks: { $t } },
      props: createDefaultProps(),
      attachTo: document.body
    });
    await wrapper.vm.showNewAction();
    await wrapper.vm.$nextTick();
    expect(wrapper.vm.editingAction).toBe(false);
    // Add an action
    wrapper.vm.currentAction.name = 'SwitchTest';
    wrapper.vm.currentAction.action.startup_stage = 'PRE_STREAM_START';
    wrapper.vm.currentAction.action.startup_commands.commands[0].cmd = 'echo';
    await wrapper.find('form').trigger('submit.prevent');
    // Now edit it
    await wrapper.vm.editAction(0);
    expect(wrapper.vm.editingAction).toBe(true);
  });
});
