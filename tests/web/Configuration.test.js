import { mount } from '@vue/test-utils'
import { describe, expect, it } from 'vitest'

import DisplayDeviceOptions from '../../src_assets/common/assets/web/configs/tabs/audiovideo/DisplayDeviceOptions.vue'
import General from '../../src_assets/common/assets/web/configs/tabs/General.vue'
import Network from '../../src_assets/common/assets/web/configs/tabs/Network.vue'

const globalOptions = {
  mocks: {
    $t: key => key,
  },
  stubs: {
    Checkbox: true,
    Info: true,
    PlatformLayout: {
      template: '<div><slot name="windows" /></div>',
    },
    Play: true,
    Plus: true,
    Shield: true,
    Trash2: true,
    TriangleAlert: true,
    Undo: true,
  },
}

describe('configuration accessibility', () => {
  it('gives every global prep command input an accessible label', () => {
    const wrapper = mount(General, {
      props: {
        platform: 'windows',
        config: {
          global_prep_cmd: [{ do: 'start', elevated: false, undo: 'stop' }],
          notify_pre_releases: false,
          system_tray: true,
        },
      },
      global: globalOptions,
    })

    for (const input of wrapper.findAll('#global_prep_cmd tbody input')) {
      expect(wrapper.get(`label[for="${input.attributes('id')}"]`).classes()).toContain('visually-hidden')
    }
  })

  it('associates labels with manual display settings and remapping inputs', () => {
    const manualWrapper = mount(DisplayDeviceOptions, {
      props: {
        platform: 'windows',
        config: {
          dd_configuration_option: 'ensure_active',
          dd_config_revert_on_disconnect: false,
          dd_hdr_option: 'disabled',
          dd_mode_remapping: {
            mixed: [],
            refresh_rate_only: [],
            resolution_only: [],
          },
          dd_refresh_rate_option: 'manual',
          dd_resolution_option: 'manual',
        },
      },
      global: globalOptions,
    })

    expect(manualWrapper.get('label[for="dd_manual_resolution"]')).toBeDefined()
    expect(manualWrapper.get('label[for="dd_manual_refresh_rate"]')).toBeDefined()

    const remappingWrapper = mount(DisplayDeviceOptions, {
      props: {
        platform: 'windows',
        config: {
          dd_configuration_option: 'ensure_active',
          dd_config_revert_on_disconnect: false,
          dd_hdr_option: 'disabled',
          dd_mode_remapping: {
            mixed: [{
              final_refresh_rate: '',
              final_resolution: '',
              requested_fps: '',
              requested_resolution: '',
            }],
            refresh_rate_only: [],
            resolution_only: [],
          },
          dd_refresh_rate_option: 'auto',
          dd_resolution_option: 'auto',
        },
      },
      global: globalOptions,
    })

    for (const input of remappingWrapper.findAll('tbody input')) {
      expect(remappingWrapper.get(`label[for="${input.attributes('id')}"]`).classes()).toContain('visually-hidden')
    }
  })
})

describe('network configuration', () => {
  it('uses the default Moonlight port when the configured port is absent', () => {
    const wrapper = mount(Network, {
      props: {
        platform: 'linux',
        config: { upnp: false },
      },
      global: globalOptions,
    })

    const tableText = wrapper.get('table').text()
    expect(tableText).toContain('47984')
    expect(tableText).toContain('47989')
    expect(tableText).toContain('48010')
    expect(tableText).not.toContain('NaN')
  })
})
