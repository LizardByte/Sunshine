import { config } from '@vue/test-utils'

// Configure Vue Test Utils globally
config.global.mocks = {}

// Mock fetch globally for all tests
global.fetch = jest.fn()

// Mock console methods to reduce noise in tests
jest.spyOn(console, 'log').mockImplementation(() => {})
jest.spyOn(console, 'warn').mockImplementation(() => {})
jest.spyOn(console, 'error').mockImplementation(() => {})

// Mock window.alert and window.confirm
global.alert = jest.fn()
global.confirm = jest.fn(() => true)

// Setup beforeEach to reset all mocks
beforeEach(() => {
  jest.clearAllMocks()
  fetch.mockClear()
})
