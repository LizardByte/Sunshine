import { mount, flushPromises } from '@vue/test-utils';
import Apps from '@/common/assets/web/components/Apps.vue';

const globalConfig = {
  global: {
    config: {
      globalProperties: {
        $t: (msg) => msg,
      },
    },
  },
};

global.fetch = jest.fn(() => Promise.resolve({
  json: () => Promise.resolve({ apps: [], platform: 'test' })
}));


  beforeEach(() => {
    jest.clearAllMocks();
  });

  it('renders the apps table', async () => {
    const wrapper = mount(Apps, globalConfig);
    await flushPromises();
    expect(wrapper.find('table').exists()).toBe(true);
    expect(wrapper.text()).toContain('applications_title');
  });

  it('fetches and displays app data', async () => {
    const mockApps = [
      { name: 'App1', output: '', cmd: [], 'exclude-global-prep-cmd': false, elevated: false, 'auto-detach': true, 'wait-all': true, 'exit-timeout': 5, 'prep-cmd': [], detached: [], 'image-path': '' },
      { name: 'App2', output: '', cmd: [], 'exclude-global-prep-cmd': false, elevated: false, 'auto-detach': true, 'wait-all': true, 'exit-timeout': 5, 'prep-cmd': [], detached: [], 'image-path': '' }
    ];
    global.fetch.mockImplementationOnce(() => Promise.resolve({
      json: () => Promise.resolve({ apps: mockApps })
    }));
    global.fetch.mockImplementationOnce(() => Promise.resolve({
      json: () => Promise.resolve({ platform: 'test' })
    }));
    const wrapper = mount(Apps, globalConfig);
    await flushPromises();
    const rows = wrapper.findAll('tbody tr');
    expect(rows.length).toBe(2);
    expect(rows[0].text()).toContain('App1');
    expect(rows[1].text()).toContain('App2');
  });

  it('shows the add new app form when add button is clicked', async () => {
    const wrapper = mount(Apps, globalConfig);
    await flushPromises();
    const addBtn = wrapper.find('button.btn.btn-primary');
    await addBtn.trigger('click');
    expect(wrapper.vm.showEditForm).toBe(true);
  });

  it('shows the edit form when edit button is clicked', async () => {
    const mockApps = [
      { name: 'App1', output: '', cmd: [], 'exclude-global-prep-cmd': false, elevated: false, 'auto-detach': true, 'wait-all': true, 'exit-timeout': 5, 'prep-cmd': [], detached: [], 'image-path': '' }
    ];
    global.fetch.mockImplementationOnce(() => Promise.resolve({
      json: () => Promise.resolve({ apps: mockApps })
    }));
    global.fetch.mockImplementationOnce(() => Promise.resolve({
      json: () => Promise.resolve({ platform: 'test' })
    }));
    const wrapper = mount(Apps, globalConfig);
    await flushPromises();
    const editBtn = wrapper.find('button.btn.btn-primary.mx-1');
    await editBtn.trigger('click');
    expect(wrapper.vm.showEditForm).toBe(true);
    expect(wrapper.vm.editForm.name).toBe('App1');
  });

  it('calls confirm and fetch on delete', async () => {
    const mockApps = [
      { name: 'App1', output: '', cmd: [], 'exclude-global-prep-cmd': false, elevated: false, 'auto-detach': true, 'wait-all': true, 'exit-timeout': 5, 'prep-cmd': [], detached: [], 'image-path': '' }
    ];
    global.fetch = jest.fn((url) => {
      if (url.includes('/api/apps/0')) {
        return Promise.resolve({
          status: 200,
          json: () => Promise.resolve({}),
        });
      }
      if (url.includes('/api/apps')) {
        return Promise.resolve({ json: () => Promise.resolve({ apps: mockApps }) });
      }
      if (url.includes('/api/config')) {
        return Promise.resolve({ json: () => Promise.resolve({ platform: 'test' }) });
      }
      return Promise.reject('Unknown endpoint');
    });
    const wrapper = mount(Apps, globalConfig);
    await flushPromises();
    window.confirm = jest.fn(() => true);
    delete window.location;
    window.location = { reload: jest.fn() };
    const delBtn = wrapper.find('button.btn.btn-danger.mx-1');
    await delBtn.trigger('click');
    await flushPromises();
    expect(window.confirm).toHaveBeenCalled();
    expect(global.fetch).toHaveBeenCalledWith('./api/apps/0', expect.objectContaining({ method: 'DELETE' }));
    expect(window.location.reload).toHaveBeenCalled();
  });
});
      json: () => Promise.resolve({ platform: 'test' })
