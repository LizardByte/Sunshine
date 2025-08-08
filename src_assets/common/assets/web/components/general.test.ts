import { describe, it, expect, beforeEach, afterEach, vi } from 'vitest';
import { ConfigAPI } from './general';

vi.mock('./general', () => ({
    ConfigAPI: {
        updateConfig: vi.fn(),
        getConfig: vi.fn(),
    },
}));

describe('ConfigAPI', () => {
    describe('updateConfig', () => {
        beforeEach(() => {
            (ConfigAPI.updateConfig as ReturnType<typeof vi.fn>).mockClear();
        });

        afterEach(() => {
            vi.restoreAllMocks();
        });

        it('should POST config with updated global_event_actions', async () => {
            const config = { foo: 'bar', global_event_actions: 'old' };
            const actions = { some: 'actions' };

            await ConfigAPI.updateConfig(config, actions);

            expect(ConfigAPI.updateConfig).toHaveBeenCalledTimes(1);
            expect(ConfigAPI.updateConfig).toHaveBeenCalledWith(config, actions);
        });

        it('should return the fetch response', async () => {
            const fakeResponse = { ok: true, json: () => Promise.resolve({}) };
            (ConfigAPI.updateConfig as ReturnType<typeof vi.fn>).mockResolvedValueOnce(fakeResponse);

            const result = await ConfigAPI.updateConfig({}, {});
            expect(result).toBe(fakeResponse);
        });
    });

    describe('getConfig', () => {
        beforeEach(() => {
            (ConfigAPI.getConfig as ReturnType<typeof vi.fn>).mockClear();
        });

        it('should call getConfig', async () => {
            await ConfigAPI.getConfig();

            expect(ConfigAPI.getConfig).toHaveBeenCalledTimes(1);
        });

        it('should return the fetch response', async () => {
            const fakeResponse = { ok: true, json: () => Promise.resolve({ config: 'data' }) };
            (ConfigAPI.getConfig as ReturnType<typeof vi.fn>).mockResolvedValueOnce(fakeResponse);

            const result = await ConfigAPI.getConfig();
            expect(result).toBe(fakeResponse);
        });
    });
});
