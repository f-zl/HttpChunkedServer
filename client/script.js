const lbCounter = document.getElementById("lbCounter")
const inpParam1 = document.getElementById("inpParam1")
const inpParam2 = document.getElementById("inpParam2")
const lbParam1 = document.getElementById("lbParam1")
const lbParam2 = document.getElementById("lbParam2")
const lbFileStatus = document.getElementById("lbFileStatus")
const lbFile = document.getElementById("lbFile")
const fileInput = document.getElementById("fileInput")

const DOMAIN = "http://127.0.0.1:8000"
async function get(url /*string*/) {
	return fetch(DOMAIN + url)
}
async function post(url /*string*/, body /*ArrayBuffer | string*/) {
	return fetch(DOMAIN + url, {
		method: "POST",
		body: body,
		/* Edge的行为：
		if body is ArrayBuffer, no Content-Type
		if body is string, Content-Type = text/plain
		两者都不会触发preflight
		*/
		mode: "cors",
	})
}
document.getElementById("btnReadPeriodic").addEventListener("click", async () => {
	const response = await get("/periodic")
	const reader = response.body.getReader()
	let i = 1
	while (true) {
		const { value, done } = await reader.read()
		if (done) break
		i += 1
		// if (i === 5) {
		// reader.cancel() // 测试下来浏览器的行为是先发FIN，好像是继续收到数据后会再发RST
		// 也就是通过关闭TCP连接来停止接收
		// break
		// }
		console.log("Received", value)
		lbCounter.innerText = value.toString()
	}
	console.log("End of receive")
})
document.getElementById("btnReadParam").addEventListener("click", async () => {
	const response = await get("/param/00000008")
	if (response.status != 200) {
		console.warn(`response ${response.statusText}`)
		console.warn(await response.text())
		return
	}
	const data = await response.bytes()
	if (data.length !== 8) {
		console.warn("Wrong response len ", data)
		return
	}
	const dv = new DataView(data.buffer)
	const param1 = dv.getUint32(0, true)
	const param2 = dv.getUint32(4, true)
	lbParam1.innerText = param1.toString(16)
	lbParam2.innerText = param2.toString(16)
})
document.getElementById("btnWriteParam").addEventListener("click", async () => {
	const param1 = Number.parseInt(inpParam1.value, 16)
	const param2 = Number.parseInt(inpParam2.value, 16)
	if (Number.isNaN(param1) || Number.isNaN(param2)) {
		console.warn("Wrong input")
		return
	}
	const data = new ArrayBuffer(10)
	const dv = new DataView(data, data)
	dv.setUint16(0, 0, true) // 前2字节是地址
	dv.setUint32(2, param1, true)
	dv.setUint32(6, param2, true)
	const response = await post("/param", data)
	if (response.status != 200) {
		console.warn(`response ${response.statusText}`)
		console.warn(await response.text())
		return
	}
})
function log(s) {
	lbFile.innerText = s
}
function fileStatusStr(file) {
	const numberOfBytes = file.size

	// Approximate to the closest prefixed unit
	const units = [
		"B",
		"KiB",
		"MiB",
		"GiB",
		"TiB",
		"PiB",
		"EiB",
		"ZiB",
		"YiB",
	]
	const exponent = Math.min(
		Math.floor(Math.log(numberOfBytes) / Math.log(1024)),
		units.length - 1,
	)
	const approx = numberOfBytes / 1024 ** exponent
	const sizeStr =
		exponent === 0
			? `${numberOfBytes} bytes`
			: `${approx.toFixed(3)} ${units[exponent]
			} (${numberOfBytes} bytes)`
	const lastModified = new Date(file.lastModified).toLocaleString()
	return `name ${file.name} size ${sizeStr} lastModified ${lastModified}`
}
document.getElementById("fileInput").addEventListener("change", () => {
	if (fileInput.files.length !== 1) {
		log("Please select a file to upload.")
		return
	}
	const file = fileInput.files[0]
	lbFileStatus.innerText = fileStatusStr(file)
	const reader = new FileReader()

	reader.onload = function (event) {
		const arrayBuffer = event.target.result
		post("/image", arrayBuffer).then(response => {
			if (response.ok) {
				log("File content uploaded successfully.")
			} else {
				log("Upload failed with status: " + response.status)
			}
		}).catch(error => {
			console.error("Error uploading file:", error)
			log("Error uploading file:" + error)
		})
	}
	reader.readAsArrayBuffer(file)
})
