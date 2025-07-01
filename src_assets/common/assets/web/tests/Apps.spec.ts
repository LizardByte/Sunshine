
import { mount, flushPromises } from '@vue/test-utils';
import Apps from '../Apps.vue';

// Mock i18n $t function globally for all tests in this file
beforeAll(() => {
  // @ts-ignore
  globalThis.$t = (msg) => msg;
});


const globalMocks = {
  global: {
    mocks: {
      $t: (msg) => msg,
    },
  },
};

global.fetch = vi.fn((url) => {
  if (url.includes('/api/apps')) {
    return Promise.resolve({ json: () => Promise.resolve({ apps: [
      { name: 'TestApp', output: 'output', cmd: 'run', index: 0, 'prep-cmd': [], detached: [], 'image-path': '' }
    ] }) });
  }
  if (url.includes('/api/config')) {
    return Promise.resolve({ json: () => Promise.resolve({ platform: 'windows' }) });
  }
  return Promise.reject('Unknown endpoint');
});

describe('Apps Screen', () => {
  it('renders app list', async () => {
    const wrapper = mount(Apps, globalMocks);
    await flushPromises();
    expect(wrapper.text()).toContain('TestApp');
  });

  it('can open edit form', async () => {
    const wrapper = mount(Apps, globalMocks);
    await flushPromises();
    await wrapper.find('button.btn-primary').trigger('click');
    expect(wrapper.vm.showEditForm).toBe(true);
  });

  it('can delete an app', async () => {
    window.confirm = vi.fn(() => true);
    const wrapper = mount(Apps, globalMocks);
    await flushPromises();
    await wrapper.find('button.btn-danger').trigger('click');
    expect(window.confirm).toHaveBeenCalled();
  });

  it('can add a new app', async () => {
    const wrapper = mount(Apps, globalMocks);
    await flushPromises();
    await wrapper.find('button.btn-primary').trigger('click');
    expect(wrapper.vm.editForm).not.toBeNull();
  });
});
