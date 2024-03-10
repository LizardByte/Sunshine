<template>
    <nav class="navbar navbar-expand-lg navbar-light" style="background-color: #ffc400">
        <div class="container-fluid">
            <a class="navbar-brand" href="/" title="Sunshine">
                <img src="/images/logo-sunshine-45.png" height="45" alt="Sunshine">
            </a>
            <button class="navbar-toggler" type="button" data-bs-toggle="collapse" data-bs-target="#navbarSupportedContent"
                aria-controls="navbarSupportedContent" aria-expanded="false" aria-label="Toggle navigation">
                <span class="navbar-toggler-icon"></span>
            </button>
            <div class="collapse navbar-collapse" id="navbarSupportedContent">
                <ul class="navbar-nav me-auto mb-2 mb-lg-0">
                    <li class="nav-item">
                        <a class="nav-link" href="/"><i class="fas fa-fw fa-home"></i> Home</a>
                    </li>
                    <li class="nav-item">
                        <a class="nav-link" href="/pin"><i class="fas fa-fw fa-unlock"></i> PIN</a>
                    </li>
                    <li class="nav-item">
                        <a class="nav-link" href="/apps"><i class="fas fa-fw fa-stream"></i> Applications</a>
                    </li>
                    <li class="nav-item">
                        <a class="nav-link" href="/config"><i class="fas fa-fw fa-cog"></i> Configuration</a>
                    </li>
                    <li class="nav-item">
                        <a class="nav-link" href="/password"><i class="fas fa-fw fa-user-shield"></i> Change Password</a>
                    </li>
                    <li class="nav-item">
                        <a class="nav-link" href="/troubleshooting"><i class="fas fa-fw fa-info"></i> Troubleshooting</a>
                    </li>
                </ul>
            </div>
        </div>
    </nav>
    <!-- Modal that is shown when the user gets a 401 error -->
    <div class="modal fade" id="loginModal" tabindex="-1">
        <div class="modal-dialog">
            <div class="modal-content">
                <div class="modal-header">
                    <h1 class="modal-title fs-5" id="exampleModalLabel">Session Expired</h1>
                </div>
                <div class="modal-body">
                    <LoginForm @loggedin="onLogin" />
                </div>
            </div>
        </div>
    </div>
</template>

<script>
import {Modal} from 'bootstrap';
import LoginForm from './LoginForm.vue'
export default {
    components: {
        LoginForm
    },
    data(){
        modal: null
    }, 
    created() {
        console.log("Header mounted!")
    },
    mounted() {
        let el = document.querySelector("a[href='" + document.location.pathname + "']");
        if (el) el.classList.add("active")
        let discordWidget = document.createElement('script')
        discordWidget.setAttribute('src', 'https://app.lizardbyte.dev/js/discord.js')
        document.head.appendChild(discordWidget)
        window.addEventListener("sunshine:session_expire", () => {
            this.modal.toggle();
        })
        this.modal = new Modal(document.getElementById('loginModal'), {});
    },
    methods: {
        onLogin(){
            this.modal.toggle();
        }
    }
}
</script>

<style>
.nav-link.active {
    font-weight: 500;
}

.form-control::placeholder {
    opacity: 0.5;
}
</style>
