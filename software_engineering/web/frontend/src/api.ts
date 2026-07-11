import axios from 'axios'
import { useAuthStore } from './store/auth'
import router from './router'

const api = axios.create({
  baseURL: '/api',
  timeout: 10000,
})

api.interceptors.request.use((cfg) => {
  const auth = useAuthStore()
  if (auth.token) {
    cfg.headers.Authorization = `Bearer ${auth.token}`
  }
  return cfg
})

api.interceptors.response.use(
  (r) => r,
  (err) => {
    if (err.response && err.response.status === 401) {
      const auth = useAuthStore()
      auth.clear()
      router.push({ name: 'login' })
    }
    return Promise.reject(err)
  },
)

export default api
