import { createClient } from '@supabase/supabase-js'

const SUPABASE_URL = import.meta.env.VITE_SUPABASE_URL;
const SUPABASE_ANON_KEY = import.meta.env.VITE_SUPABASE_KEY;

const cliente = createClient(SUPABASE_URL, SUPABASE_ANON_KEY);

const cuerpoTabla = document.getElementById('cuerpo-tabla');

function formatearNumero(valor) {
    return valor === null || valor === undefined ? '—' : Number(valor).toFixed(2);
}

function formatearFecha(fecha) {
    if (!fecha) return '—';
    const [anio, mes, dia] = fecha.split('-');
    return `${dia}/${mes}/${anio}`;
}

function crearCelda(texto, clase) {
    const td = document.createElement('td');
    td.textContent = texto;
    if (clase) td.className = clase;
    return td;
}

function mostrarMensaje(texto) {
    cuerpoTabla.replaceChildren();
    const fila = document.createElement('tr');
    const celda = crearCelda(texto);
    celda.colSpan = 8;
    fila.appendChild(celda);
    cuerpoTabla.appendChild(fila);
}

const INTERVALO_ACTUALIZACION_MS = 5000;

let primeraCarga = true;  
let tablaConDatos = false; 
let cargando = false;      

async function cargarRegistros() {
    if (cargando) return;
    cargando = true;

    if (primeraCarga) {
        mostrarMensaje('Cargando registros...');
        primeraCarga = false;
    }

    const { data, error } = await cliente
        .from('registros')
        .select('*')
        .order('id', { ascending: false })
        .limit(30);

    cargando = false;

    if (error) {
        console.error('Error al consultar Supabase:', error);
        if (!tablaConDatos) mostrarMensaje('No se pudieron cargar los registros.');
        return;
    }

    if (data.length === 0) {
        tablaConDatos = false;
        mostrarMensaje('Todavía no hay registros.');
        return;
    }

    tablaConDatos = true;
    cuerpoTabla.replaceChildren();

    for (const r of data) {
        const fila = document.createElement('tr');
        if (r.alarma) fila.classList.add('fila-alarma'); 
        fila.append(
            crearCelda(r.id),
            crearCelda(formatearNumero(r.temp_c)),
            crearCelda(formatearNumero(r.temp_f)),
            crearCelda(formatearNumero(r.temp_k)),
            crearCelda(formatearNumero(r.temp_r)),
            crearCelda(formatearFecha(r.fecha)),
            crearCelda(r.hora ?? '—'),
            crearCelda(
                r.alarma ? 'Alarma' : 'Normal',
                r.alarma ? 'estatus-alarma' : 'estatus-normal'
            )
        );
        cuerpoTabla.appendChild(fila);
    }
}

const MINIMO_REGISTROS_DESCARGA = 5; 

function descargarTabla() {
    const filasDatos = [...cuerpoTabla.rows].filter((fila) => fila.cells.length === 8);

    if (filasDatos.length < MINIMO_REGISTROS_DESCARGA) {
        alert(
            `Se necesitan al menos ${MINIMO_REGISTROS_DESCARGA} registros para descargar ` +
            `(actualmente hay ${filasDatos.length}).`
        );
        return;
    }

    const encabezados = [...document.querySelectorAll('thead th')].map((th) => th.textContent.trim());
    const filas = filasDatos.map((fila) => [...fila.cells].map((celda) => celda.textContent));

    const texto = [encabezados, ...filas].map((columnas) => columnas.join('\t')).join('\r\n');

    const blob = new Blob(['\uFEFF' + texto], { type: 'text/plain;charset=utf-8' });
    const url = URL.createObjectURL(blob);

    const enlace = document.createElement('a');
    enlace.href = url;
    enlace.download = 'registros_dht11.txt';
    document.body.appendChild(enlace);
    enlace.click();
    enlace.remove();
    URL.revokeObjectURL(url);
}

document.getElementById('descargar').addEventListener('click', descargarTabla);

let eliminando = false; 

async function limpiarRegistros() {
    if (eliminando) return;

    if (!confirm('¿Seguro que quieres eliminar TODOS los registros? Esta acción no se puede deshacer.')) {
        return;
    }

    eliminando = true;

    const { error, count } = await cliente
        .from('registros')
        .delete({ count: 'exact' })
        .not('id', 'is', null);

    eliminando = false;

    if (error) {
        console.error('Error al eliminar los registros:', error);
        alert('No se pudieron eliminar los registros.');
        return;
    }

    if (count === 0 && tablaConDatos) {
        alert('No se eliminó ningún registro. Revisa que exista una política de DELETE para la tabla "registros".');
        return;
    }

    tablaConDatos = false;
    mostrarMensaje('Todavía no hay registros.');
}

document.getElementById('limpiar').addEventListener('click', limpiarRegistros);

cargarRegistros();
setInterval(cargarRegistros, INTERVALO_ACTUALIZACION_MS);