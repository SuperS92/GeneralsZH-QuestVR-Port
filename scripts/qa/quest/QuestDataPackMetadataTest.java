// SPDX-License-Identifier: GPL-3.0-or-later
package com.generalsx.zerohour;

/** Tests production metadata helpers without Android activity or network access. */
public final class QuestDataPackMetadataTest {
    private static int checks;
    private static void expect(String actual, String expected) {
        if (!expected.equals(actual)) throw new AssertionError(actual + " != " + expected);
        ++checks;
    }
    public static void main(String[] args) throws Exception {
        // Measured publisher metadata and independently downloaded package digest.
        String actual = "a4d07f8efc2ba08c416c17776aed0f71836d3b55f7899bb313f7626718bab6e5";
        String published = " A4D07F8EFC2BA08C416C17776AED0F71836D3B55F7899BB313F7626718BAB6E5";
        expect(DataPackInstaller.normalizeExpectedSha(published, true), published.substring(1));
        expect(DataPackInstaller.normalizeExpectedSha(published, false), published.substring(1));
        expect(DataPackInstaller.normalizeExpectedSha("\t" + actual + "\r\n", true), actual);
        if (!DataPackInstaller.normalizeExpectedSha(published, true).equalsIgnoreCase(actual))
            throw new AssertionError("Published QFE6A digest rejected");
        ++checks;
        for (String wrong : new String[] {
                "0E455B86C4542DD850926B84D8856D4AA2EA5C594F847E798AAC6D6A5F40305A",
                "a4d07f8efc2ba08c416c17776aed0f71836d3b55f7899bb313f7626718bab6e4"}) {
            if (DataPackInstaller.normalizeExpectedSha(wrong, true).equalsIgnoreCase(actual))
                throw new AssertionError("Different digest accepted");
            ++checks;
        }
        for (String invalid : new String[] {"", " \t ", "abcd", published + "0",
                "g4d07f8efc2ba08c416c17776aed0f71836d3b55f7899bb313f7626718bab6e5",
                "a4d07f8e fc2ba08c416c17776aed0f71836d3b55f7899bb313f7626718bab6e5"}) {
            try {
                DataPackInstaller.normalizeExpectedSha(invalid, true);
                throw new AssertionError("Missing or malformed digest accepted");
            } catch (java.io.IOException expected) {
                ++checks;
            }
        }
        // Exercise the actual upstream marker reader, including a marker split across reads.
        java.lang.reflect.Method reader = DataPackInstaller.class.getDeclaredMethod(
            "pcExeCrcState", java.io.InputStream.class, long[].class);
        reader.setAccessible(true);
        byte[] marker = "MARKER:OfficialLogicCRCRevision".getBytes("US-ASCII");
        for (int offset : new int[]{0, 65520, 65536, 131060}) {
            byte[] input = new byte[offset + marker.length + 17];
            System.arraycopy(marker, 0, input, offset, marker.length);
            long[] state = {-1, -1};
            reader.invoke(null, new java.io.ByteArrayInputStream(input), state);
            if (state[1] != 1) throw new AssertionError("Marker missed at " + offset);
            ++checks;
        }
        for (byte[] input : new byte[][]{new byte[0], new byte[150000],
                "MARKER:OfficialLogicCRCRevisioX".getBytes("US-ASCII"),
                "MARKER:OfficialLogicCRCRevision".substring(0, 15).getBytes("US-ASCII")}) {
            long[] state = {-1, -1};
            reader.invoke(null, new java.io.ByteArrayInputStream(input), state);
            if (state[1] != 0) throw new AssertionError("False revision marker");
            ++checks;
        }
        // Optional private actual PC EXE fixture; no bytes or paths enter source.
        if (args.length > 0) {
            long[] state = {-1, -1};
            try (java.io.InputStream input = new java.io.FileInputStream(args[0])) {
                reader.invoke(null, input, state);
            }
            if (state[0] != 147532298L || state[1] != 0)
                throw new AssertionError("QFE6A checksum/revision changed");
            ++checks;
        }
        System.out.println("PASS: " + checks + " production metadata checks");
    }
}
