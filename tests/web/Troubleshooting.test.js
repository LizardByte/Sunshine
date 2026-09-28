import { mount, flushPromises } from '@vue/test-utils'
import { afterEach, describe, expect, it, vi } from 'vitest'

vi.mock('../../src_assets/common/assets/web/Navbar.vue', () => ({
  default: { template: '<div />' },
}))

import Troubleshooting from '../../src_assets/common/assets/web/Troubleshooting.vue'

async function mountTroubleshooting(platform, gamepadDriver, licenseStatus = {}, permissions = []) {
  vi.stubGlobal('fetch', vi.fn(async (url) => {
    if (url === '/api/config') {
      return { json: async () => ({ platform, gamepad_driver: gamepadDriver }) }
    }
    if (url === '/api/virtual-input/status') {
      return {
        json: async () => ({
          virtualhid: {
            installed: true,
            version: '2026.914.1218',
            version_compatible: true,
            supported_versions: '>= 2026.914.1218',
          },
          vigembus: { installed: false },
        }),
      }
    }
    if (url === '/api/virtual-input/license') {
      return { ok: true, json: async () => ({ service_available: true, state: 'licensed', licensed: true, ...licenseStatus }) }
    }
    if (url === './api/permissions') {
      return { ok: true, json: async () => ({ permissions }) }
    }
    if (url === './api/permissions/request') {
      return { ok: true, json: async () => ({ status: true }) }
    }
    if (url === './api/logs') {
      return { text: async () => '' }
    }
    if (url === './api/clients/list') {
      return { json: async () => ({ status: true, named_certs: [] }) }
    }
    return {
      ok: true,
      json: async () => ({ tag_name: 'v2026.914.1218.10', html_url: 'https://example.com/release' }),
    }
  }))

  const wrapper = mount(Troubleshooting, {
    global: {
      mocks: { $t: key => key },
      stubs: { Navbar: true, RouterLink: true },
    },
  })
  await flushPromises()
  return wrapper
}

afterEach(() => {
  vi.unstubAllGlobals()
})

describe('virtual input troubleshooting', () => {
  it('orders permission rows consistently across platforms', async () => {
    const permissions = [
      { id: 'local_network', status: 'on_use', required: true },
      { id: 'config_directory', status: 'granted', required: true },
      { id: 'microphone', status: 'granted', required: false },
      { id: 'system_audio', status: 'on_use', required: true },
      { id: 'notifications', status: 'granted', required: false },
      { id: 'input', status: 'granted', required: true },
      { id: 'screen_recording', status: 'granted', required: true },
    ]
    for (const platform of ['macos', 'windows']) {
      const wrapper = await mountTroubleshooting(platform, 'none', {}, permissions)
      expect(wrapper.findAll('.permission-table-shell tbody tr').map(row => row.get('strong').text())).toEqual([
        'troubleshooting.permission_screen_recording',
        'troubleshooting.permission_input',
        'troubleshooting.permission_notifications',
        'troubleshooting.permission_microphone',
        'troubleshooting.permission_system_audio',
        'troubleshooting.permission_local_network',
        'troubleshooting.permission_config_directory',
      ])
      wrapper.unmount()
    }
  })

  it('shows macOS permission status and requests access from the row button', async () => {
    const wrapper = await mountTroubleshooting('macos', 'all', {}, [
      { id: 'screen_recording', status: 'denied', required: true, requestable: true },
      { id: 'notifications', status: 'granted', required: false },
      { id: 'system_audio', status: 'on_use', required: true, requestable: true },
    ])

    expect(wrapper.get('#permissions').text()).toBe('troubleshooting.permissions_title')
    const card = wrapper.findAll('.card').find(item => item.find('#permissions').exists())
    expect(card.findAll('.permission-table-shell tbody tr')).toHaveLength(3)
    expect(card.get('.permission-table-shell thead').text()).toContain('troubleshooting.permissions_requirement')
    expect(card.findAll('.permission-table-shell tbody button')).toHaveLength(2)
    const screenRow = card.findAll('.permission-table-shell tbody tr').find(row => row.text().includes('permission_screen_recording'))
    expect(screenRow.get('.status-icon').classes()).toContain('status-icon-danger')
    expect(screenRow.get('.status-icon svg').exists()).toBe(true)
    expect(screenRow.get('.status-icon .visually-hidden').text()).toBe('troubleshooting.permissions_status_denied')
    expect(screenRow.get('.status-icon').attributes('title')).toBe('troubleshooting.permissions_status_denied')
    expect(screenRow.get('.status-icon').attributes('role')).toBeUndefined()
    expect(screenRow.get('.status-icon').attributes('tabindex')).toBeUndefined()
    const audioRow = card.findAll('.permission-table-shell tbody tr').find(row => row.text().includes('permission_system_audio'))
    expect(audioRow.get('.status-icon').classes()).toContain('status-icon-primary')
    expect(audioRow.get('.status-icon').attributes('title')).toBe('troubleshooting.permissions_status_on_use')
    await screenRow.get('button').trigger('click')
    await flushPromises()
    expect(fetch).toHaveBeenCalledWith('./api/permissions/request', expect.objectContaining({
      method: 'POST',
      body: JSON.stringify({ id: 'screen_recording' }),
    }))
    expect(screenRow.text()).toContain('troubleshooting.permission_screen_recording_help')
    wrapper.unmount()
  })

  it('shows macOS input access recovery steps when the request button is used', async () => {
    const wrapper = await mountTroubleshooting('macos', 'all', {}, [
      { id: 'input', status: 'denied', required: true, requestable: true },
    ])

    const row = wrapper.get('.permission-table-shell tbody tr')
    await row.get('button').trigger('click')
    await flushPromises()
    expect(fetch).toHaveBeenCalledWith('./api/permissions/request', expect.objectContaining({
      method: 'POST',
      body: JSON.stringify({ id: 'input' }),
    }))
    expect(row.text()).toContain('troubleshooting.permission_input_help')
    wrapper.unmount()
  })

  it('shows Local Network setup steps after opening Privacy & Security', async () => {
    const wrapper = await mountTroubleshooting('macos', 'all', {}, [
      { id: 'local_network', status: 'on_use', required: true, requestable: true },
    ])

    const row = wrapper.get('.permission-table-shell tbody tr')
    expect(row.get('button').text()).toBe('troubleshooting.permissions_privacy_settings')
    await row.get('button').trigger('click')
    await flushPromises()
    expect(fetch).toHaveBeenCalledWith('./api/permissions/request', expect.objectContaining({
      method: 'POST',
      body: JSON.stringify({ id: 'local_network' }),
    }))
    expect(row.text()).toContain('troubleshooting.permission_local_network_help')
    wrapper.unmount()
  })

  it('shows Windows directory access with setup steps', async () => {
    const wrapper = await mountTroubleshooting('windows', 'all', {}, [
      { id: 'config_directory', status: 'denied', required: true, verifiable: true, requestable: false },
    ])
    expect(wrapper.find('#permissions').exists()).toBe(true)
    const row = wrapper.get('.permission-table-shell tbody tr')
    await row.get('button').trigger('click')
    expect(row.find('output').exists()).toBe(true)
    expect(row.text()).toContain('troubleshooting.permission_config_directory_help')
    expect(fetch).not.toHaveBeenCalledWith('./api/permissions/request', expect.anything())
    wrapper.unmount()
  })

  it('shows Linux input setup steps', async () => {
    const wrapper = await mountTroubleshooting('linux', 'none', {}, [
      { id: 'input', status: 'denied', required: true, verifiable: true, requestable: false },
    ])
    await wrapper.get('.permission-table-shell tbody button').trigger('click')
    expect(wrapper.text()).toContain('troubleshooting.permission_input_help_linux')
    wrapper.unmount()
  })

  it('shows the broker version table without ViGEmBus on macOS', async () => {
    const wrapper = await mountTroubleshooting('macos', 'all')

    expect(wrapper.find('.virtual-gamepad-section').exists()).toBe(true)
    expect(wrapper.findAll('.driver-table-shell tbody tr')).toHaveLength(1)
    expect(wrapper.find('.driver-table-shell').text()).toContain('2026.914.1218')
    const status = wrapper.get('.driver-status-icon')
    expect(status.classes()).toContain('status-icon-success')
    expect(status.get('svg').exists()).toBe(true)
    expect(status.get('.visually-hidden').text()).toBe('troubleshooting.driver_status_compatible')
    expect(status.attributes('title')).toBe('troubleshooting.driver_status_compatible')
    expect(status.attributes('role')).toBeUndefined()
    expect(status.attributes('tabindex')).toBeUndefined()
    expect(fetch).toHaveBeenCalledWith('/api/virtual-input/status')
    wrapper.unmount()
  })

  it.each(['macos', 'windows'])('shows only the license activation limit on %s', async platform => {
    const wrapper = await mountTroubleshooting(platform, 'all', { activation_usage: 1, activation_limit: 5 })

    const activation = wrapper.findAll('.virtualhid-license-stat').find(stat =>
      stat.find('dt').text() === 'troubleshooting.virtualhid_license_activation_limit'
    )
    expect(activation.find('dd').text()).toBe('5')
    wrapper.unmount()
  })

  it('keeps both backend rows on Windows when all backends are enabled', async () => {
    const wrapper = await mountTroubleshooting('windows', 'all')

    expect(wrapper.findAll('.driver-table-shell tbody tr')).toHaveLength(2)
    const vigembusStatus = wrapper.get('#vigembus .driver-status-icon')
    expect(vigembusStatus.classes()).toContain('status-icon-neutral')
    expect(vigembusStatus.attributes('title')).toBe('troubleshooting.driver_status_not_installed')
    expect(vigembusStatus.get('.visually-hidden').text()).toBe('troubleshooting.driver_status_not_installed')
    wrapper.vm.virtualhid.version_compatible = false
    await wrapper.vm.$nextTick()
    const unsupportedStatus = wrapper.get('.driver-table-shell tbody tr:first-child .driver-status-icon')
    expect(unsupportedStatus.classes()).toContain('status-icon-danger')
    expect(unsupportedStatus.attributes('title')).toBe('troubleshooting.driver_status_unsupported')
    expect(unsupportedStatus.get('.visually-hidden').text()).toBe('troubleshooting.driver_status_unsupported')
    wrapper.unmount()
  })

  it('shows distinct icons for granted, unrequested, and unknown permissions', async () => {
    const wrapper = await mountTroubleshooting('macos', 'all', {}, [
      { id: 'notifications', status: 'granted', required: false },
      { id: 'microphone', status: 'not_determined', required: false, requestable: true },
      { id: 'input', status: 'unknown', required: true },
    ])

    const icons = wrapper.findAll('.permission-table-shell tbody .status-icon')
    expect(icons.every(icon => icon.find('svg').exists())).toBe(true)
    expect(icons.map(icon => icon.classes().find(name => name.startsWith('status-icon-')))).toEqual([
      'status-icon-warning', 'status-icon-success', 'status-icon-neutral',
    ])
    expect(icons.map(icon => icon.attributes('title'))).toEqual([
      'troubleshooting.permissions_status_unknown',
      'troubleshooting.permissions_status_granted',
      'troubleshooting.permissions_status_not_determined',
    ])
    expect(wrapper.findAll('.permission-table-shell tbody button')).toHaveLength(2)
    wrapper.unmount()
  })
})
