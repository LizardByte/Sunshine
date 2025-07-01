
import { shallowMount, mount } from '@vue/test-utils';
import Apps from './Apps.vue';
import { convertPrepCmdsToEventActions } from '../web/utils/convertPrepCmd.js';

import { vi } from 'vitest';


// Only mock for unit tests, not for E2E test
import * as convertPrepCmdModule from '../web/utils/convertPrepCmd.js';

import { describe, it, beforeEach, beforeAll, expect, vi } from 'vitest';

describe('Apps.vue - prep command conversion', () => {
  let wrapper;
  const dummyApps = { apps: [] };
  const dummyConfig = { platform: 'windows' };

  beforeAll(() => {
    // Mock fetch globally
    globalThis.fetch = vi.fn((url) => {
      if (typeof url === 'string' && url.includes('api/apps')) {
        return Promise.resolve({ json: () => Promise.resolve(dummyApps) });
      }
      if (typeof url === 'string' && url.includes('api/config')) {
        return Promise.resolve({ json: () => Promise.resolve(dummyConfig) });
      }
      return Promise.resolve({ json: () => Promise.resolve({}) });
    });
  });

  beforeEach(() => {
    // Mock alert before each test
    window.alert = vi.fn();
    global.alert = vi.fn();
    // Default: shallowMount for unit tests
    wrapper = shallowMount(Apps, {
      global: {
        stubs: ['Navbar', 'Checkbox', 'EventActionsEditor'],
        mocks: {
          $t: (msg) => msg
        }
      }
    });
    wrapper.vm.editForm = {
      name: 'TestApp',
      'prep-cmd': [
        { do: 'echo 1', undo: 'echo 2', elevated: true },
        { do: 'echo 3', undo: 'echo 4' }
      ]
    };
  });


  it('calls convertPrepCmdsToEventActions with correct arguments', () => {
    // Mock for this unit test only
    vi.spyOn(convertPrepCmdModule, 'convertPrepCmdsToEventActions').mockReturnValue({ stages: { stage1: {} } });
    wrapper.vm.convertAppPrepCmdToEventActions();
    expect(convertPrepCmdModule.convertPrepCmdsToEventActions).toHaveBeenCalledWith([
      { do_cmd: 'echo 1', undo_cmd: 'echo 2', elevated: true },
      { do_cmd: 'echo 3', undo_cmd: 'echo 4', elevated: false }
    ]);
    vi.restoreAllMocks();
  });


  it('updates event-actions and clears prep-cmd on success', () => {
    vi.spyOn(convertPrepCmdModule, 'convertPrepCmdsToEventActions').mockReturnValue([{ name: 'Test Action', action: { foo: 'bar' } }]);
    wrapper.vm.convertAppPrepCmdToEventActions();
    expect(wrapper.vm.editForm['event-actions']).toEqual([{ name: 'Test Action', action: { foo: 'bar' } }]);
    expect(wrapper.vm.editForm['prep-cmd']).toEqual([]);
    expect(window.alert).toHaveBeenCalledWith('Successfully converted prep commands to event actions!');
    vi.restoreAllMocks();
  });


  it('alerts on conversion failure (no commands)', () => {
    vi.spyOn(convertPrepCmdModule, 'convertPrepCmdsToEventActions').mockReturnValue([]);
    wrapper.vm.convertAppPrepCmdToEventActions();
    expect(window.alert).toHaveBeenCalledWith('Conversion failed: No commands to convert.');
    vi.restoreAllMocks();
  });

  it('alerts on error thrown', () => {
    vi.spyOn(convertPrepCmdModule, 'convertPrepCmdsToEventActions').mockImplementation(() => { throw new Error('fail'); });
    wrapper.vm.convertAppPrepCmdToEventActions();
    expect(window.alert).toHaveBeenCalledWith('Conversion failed: fail');
    vi.restoreAllMocks();
  });

  it('shows EventActionsEditor after converting prep commands (end-to-end)', async () => {
    // Use the real conversion function
    vi.restoreAllMocks();
    // Mock alerts
    global.alert = vi.fn();
    window.alert = vi.fn();
    
    const TestEventActionsEditor = {
      name: 'EventActionsEditor',
      template: '<div data-testid="event-actions-editor"></div>',
      props: ['modelValue']
    };
    const e2eWrapper = mount(Apps, {
      global: {
        stubs: {
          Navbar: true,
          Checkbox: true,
          EventActionsEditor: TestEventActionsEditor
        },
        mocks: {
          $t: (msg) => msg,
          alert: vi.fn()
        }
      }
    });
    // Set up editForm and showEditForm so the button is visible
    await e2eWrapper.setData({
      showEditForm: true,
      editForm: {
        name: 'TestApp',
        'prep-cmd': [
          { do: 'echo 1', undo: 'echo 2', elevated: true },
          { do: 'echo 3', undo: 'echo 4' }
        ],
        'event-actions': undefined // Start with no event actions
      }
    });
    
    // Verify initial state - EventActionsEditor should NOT be visible
    expect(e2eWrapper.find('[data-testid="event-actions-editor"]').exists()).toBe(false);
    
    // Find and click the convert button
    const btn = e2eWrapper.find('button.btn-primary');
    expect(btn.exists()).toBe(true);
    await btn.trigger('click');
    
    // Wait for the conversion to complete
    await e2eWrapper.vm.$nextTick();
    await new Promise(resolve => setTimeout(resolve, 100));
    
    // Debug: check what's in editForm after conversion
    console.log('editForm after conversion:', e2eWrapper.vm.editForm);
    
    // EventActionsEditor should now be rendered
    expect(e2eWrapper.find('[data-testid="event-actions-editor"]').exists()).toBe(true);
    // The edit form should remain visible (UI logic changed)
    expect(e2eWrapper.find('.edit-form').exists()).toBe(true);
  });
});
