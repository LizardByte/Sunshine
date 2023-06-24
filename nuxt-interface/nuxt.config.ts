// https://nuxt.com/docs/api/configuration/nuxt-config
export default defineNuxtConfig({
	app: {
		baseURL: "/sunshine/",
	},

	devtools: { enabled: true },
	modules: ["@element-plus/nuxt", "@pinia/nuxt"],
	typescript: {
		typeCheck: true,
	},
});
