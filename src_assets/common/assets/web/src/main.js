import 'bootstrap/dist/css/bootstrap.min.css'
import '@/assets/css/sunshine.css';
import '@fortawesome/fontawesome-free/css/all.min.css'
import { createApp } from "vue";
import { initApp } from "./utils/init.js";
import App from './App.vue'

console.log("Hello, Sunshine!");
let app = createApp(App);

initApp(app);