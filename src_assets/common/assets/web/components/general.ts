import { ref, watch, computed } from 'vue';

function safeJsonParse(input: any, fallback: any) {
    if (input === null || input === undefined) return fallback;
    if (typeof input !== 'string') {
        // If it's already parsed (object/array), return it
        if (typeof input === 'object') return input;
        return fallback;
    }
    try {
        if (!input || input.trim() === '' || input === 'undefined' || input === 'null') return fallback;
        return JSON.parse(input);
    } catch (e) {
        console.warn('Failed to parse JSON:', input, 'Error:', e);
        return fallback;
    }
}

export default function useGeneral(config: any) {
    const migrationSuggested = computed(() => {
        try {
            if (!config || (!config.value && typeof config !== 'object')) return false;
            const configVal = config.value || config;
            if (!configVal || typeof configVal !== 'object') return false;
            const prep = Array.isArray(configVal.global_prep_cmd) ? configVal.global_prep_cmd : safeJsonParse(configVal.global_prep_cmd, []);
            const actions = safeJsonParse(configVal.global_event_actions, []);
            const prepNotEmpty = Array.isArray(prep) && prep.length > 0;
            const actionsMissingOrEmpty = !Array.isArray(actions) || actions.length === 0;
            return prepNotEmpty && actionsMissingOrEmpty;
        } catch (e) {
            console.error('[useGeneral] Error in migrationSuggested computed:', e);
            return false;
        }
    });

    // Expose config API methods in composition
    async function getConfig() {
        return await ConfigAPI.getConfig();
    }
    async function updateConfig(configObj: any, actions: any) {
        return await ConfigAPI.updateConfig(configObj, actions);
    }

    const parsedEventActions = computed(() => {
        try {
            if (!config || (!config.value && typeof config !== 'object')) return [];
            const configVal = config.value || config;
            if (!configVal || typeof configVal !== 'object') return [];
            const actions = safeJsonParse(configVal.global_event_actions, []);
            return Array.isArray(actions) ? actions : [];
        } catch (e) {
            console.error('[useGeneral] Error in parsedEventActions computed:', e);
            return [];
        }
    });

    function convertLegacyGlobalPrepCmd(globalPrepCmd: LegacyCmd[]): { pre: EventAction, post: EventAction } {
        // PRE: all 'do' commands, including empty, in order
        const preCommands = globalPrepCmd
            .map((cmd: LegacyCmd) => ({
                cmd: cmd.do,
                admin: !!cmd.elevated,
                async: false,
                timeout: 30
            }));
        // POST: all 'undo' commands, including empty, in reverse order
        const postCommands = globalPrepCmd
            .map((cmd: LegacyCmd) => ({
                cmd: cmd.undo,
                admin: !!cmd.elevated,
                async: false,
                timeout: 30
            }))
            .reverse();
        return {
            pre: {
                stage: 'PRE_STREAM_START',
                failure_policy: 'FAIL_FAST',
                commands: preCommands
            },
            post: {
                stage: 'POST_STREAM_STOP',
                failure_policy: 'CONTINUE_ON_ERROR',
                commands: postCommands
            }
        };
    }

    return {
        migrationSuggested,
        getConfig,
        updateConfig,
        parsedEventActions,
        convertLegacyGlobalPrepCmd
    };
}

interface LegacyCmd {
    do: string;
    elevated: boolean;
    undo: string;
}

interface EventActionCmd {
    cmd: string;
    admin: boolean;
    async: boolean;
    timeout: number;
}

interface EventAction {
    stage: string;
    failure_policy: string;
    commands: EventActionCmd[];
}

/**
 * Converts legacy globalPrepCmd array to global_event_actions format.
 * Includes all entries, even if 'do' or 'undo' are empty strings.
 * @param {LegacyCmd[]} globalPrepCmd - The legacy array of command objects.
 * @returns {{ pre: EventAction, post: EventAction }}
 */


/**
 * ConfigAPI provides static methods to get and update the config via the API.
 * This class is mockable for testing.
 */
export class ConfigAPI {
    /**
     * Fetches the current config from the API.
     * @returns {Promise<any>} The parsed config object.
     */
    static async getConfig(): Promise<any> {
        const response = await fetch("./api/config");
        if (!response.ok) throw new Error("Failed to fetch config");
        return response.json();
    }

    /**
     * Saves the entire config object to the config file via the API, with updated global_event_actions.
     * @param {any} config - The full config object to save.
     * @param {any} actions - The global_event_actions object to set and save.
     * @returns {Promise<Response>} The fetch response promise.
     */
    static async updateConfig(config: any, actions: any): Promise<Response> {
        // Clone config to avoid mutating the original
        const configToSave = { ...config };
        configToSave.global_event_actions = JSON.stringify(actions);
        return fetch("./api/config", {
            method: "POST",
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(configToSave)
        });
    }
}