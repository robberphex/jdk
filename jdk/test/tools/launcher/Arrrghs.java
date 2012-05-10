/*
 * Copyright (c) 2007, 2012, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 */

/**
 * @test
 * @bug 5030233 6214916 6356475 6571029 6684582 6742159 4459600 6758881 6753938
 *      6894719 6968053 7151314
 * @summary Argument parsing validation.
 * @compile -XDignore.symbol.file Arrrghs.java
 * @run main Arrrghs
 */

import java.io.BufferedReader;
import java.io.File;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.util.Map;

public class Arrrghs extends TestHelper {
    private Arrrghs(){}
    /**
     * This class provides various tests for arguments processing.
     * A group of tests to ensure that arguments are passed correctly to
     * a child java process upon a re-exec, this typically happens when
     * a version other than the one being executed is requested by the user.
     *
     * History: these set of tests  were part of Arrrghs.sh. The MKS shell
     * implementations were notoriously buggy. Implementing these tests purely
     * in Java is not only portable but also robust.
     *
     */

    // The version string to force a re-exec
    final static String VersionStr = "-version:1.1+";

    // The Cookie or the pattern we match in the debug output.
    final static String Cookie = "ReExec Args: ";

    /*
     * SIGH, On Windows all strings are quoted, we need to unwrap it
     */
    private static String removeExtraQuotes(String in) {
        if (isWindows) {
            // Trim the string and remove the enclosed quotes if any.
            in = in.trim();
            if (in.startsWith("\"") && in.endsWith("\"")) {
                return in.substring(1, in.length()-1);
            }
        }
        return in;
    }

    /*
     * This method detects the cookie in the output stream of the process.
     */
    private static boolean detectCookie(InputStream istream,
            String expectedArguments) throws IOException {
        BufferedReader rd = new BufferedReader(new InputStreamReader(istream));
        boolean retval = false;

        String in = rd.readLine();
        while (in != null) {
            if (debug) System.out.println(in);
            if (in.startsWith(Cookie)) {
                String detectedArgument = removeExtraQuotes(in.substring(Cookie.length()));
                if (expectedArguments.equals(detectedArgument)) {
                    retval = true;
                } else {
                    System.out.println("Error: Expected Arguments\t:'" +
                            expectedArguments + "'");
                    System.out.println(" Detected Arguments\t:'" +
                            detectedArgument + "'");
                }
                // Return the value asap if not in debug mode.
                if (!debug) {
                    rd.close();
                    istream.close();
                    return retval;
                }
            }
            in = rd.readLine();
        }
        return retval;
    }

    private static boolean doTest0(ProcessBuilder pb, String expectedArguments) {
        boolean retval = false;
        try {
            pb.redirectErrorStream(true);
            Process p = pb.start();
            retval = detectCookie(p.getInputStream(), expectedArguments);
            p.waitFor();
            p.destroy();
        } catch (Exception ex) {
            ex.printStackTrace();
            throw new RuntimeException(ex.getMessage());
        }
        return retval;
    }

    /**
     * This method return true  if the expected and detected arguments are the same.
     * Quoting could cause dissimilar testArguments and expected arguments.
     */
    static int doTest(String testArguments, String expectedPattern) {
        ProcessBuilder pb = new ProcessBuilder(javaCmd,
                VersionStr, testArguments);

        Map<String, String> env = pb.environment();
        env.put("_JAVA_LAUNCHER_DEBUG", "true");
        return doTest0(pb, testArguments) ? 0 : 1;
    }

    /**
     * A convenience method for identical test pattern and expected arguments
     */
    static int doTest(String testPattern) {
        return doTest(testPattern, testPattern);
    }

    static void quoteParsingTests() {
        /*
         * Tests for 6214916
         * These tests require that a JVM (any JVM) be installed in the system registry.
         * If none is installed, skip this test.
         */
        TestResult tr = doExec(javaCmd, VersionStr, "-version");
        if (!tr.isOK()) {
            System.err.println("Warning:Argument Passing Tests were skipped, " +
                    "no java found in system registry.");
            return;
        }

        // Basic test
        testExitValue += doTest("-a -b -c -d");

        // Basic test with many spaces
        testExitValue += doTest("-a    -b      -c       -d");

        // Quoted whitespace does matter ?
        testExitValue += doTest("-a \"\"-b      -c\"\" -d");


        // Escaped quotes outside of quotes as literals
        testExitValue += doTest("-a \\\"-b -c\\\" -d");

        // Check for escaped quotes inside of quotes as literal
        testExitValue += doTest("-a \"-b \\\"stuff\\\"\" -c -d");

        // A quote preceeded by an odd number of slashes is a literal quote
        testExitValue += doTest("-a -b\\\\\\\" -c -d");

        // A quote preceeded by an even number of slashes is a literal quote
        // see 6214916.
        testExitValue += doTest("-a -b\\\\\\\\\" -c -d");

        // Make sure that whitespace doesn't interfere with the removal of the
        // appropriate tokens. (space-tab-space preceeds -jre-restict-search).
        testExitValue += doTest("-a -b  \t -jre-restrict-search -c -d","-a -b -c -d");

        // Make sure that the mJRE tokens being stripped, aren't stripped if
        // they happen to appear as arguments to the main class.
        testExitValue += doTest("foo -version:1.1+");

        System.out.println("Completed arguments quoting tests with " +
                testExitValue + " errors");
    }

    /*
     * These tests are usually run on non-existent targets to check error results
     */
    static void runBasicErrorMessageTests() {
        // Tests for 5030233
        TestResult tr = doExec(javaCmd, "-cp");
        tr.checkNegative();
        tr.isNotZeroOutput();
        System.out.println(tr);

        tr = doExec(javaCmd, "-classpath");
        tr.checkNegative();
        tr.isNotZeroOutput();
        System.out.println(tr);

        tr = doExec(javaCmd, "-jar");
        tr.checkNegative();
        tr.isNotZeroOutput();
        System.out.println(tr);

        tr = doExec(javacCmd, "-cp");
        tr.checkNegative();
        tr.isNotZeroOutput();
        System.out.println(tr);

        // Test for 6356475 "REGRESSION:"java -X" from cmdline fails"
        tr = doExec(javaCmd, "-X");
        tr.checkPositive();
        tr.isNotZeroOutput();
        System.out.println(tr);

        tr = doExec(javaCmd, "-help");
        tr.checkPositive();
        tr.isNotZeroOutput();
        System.out.println(tr);

        // 6753938, test for non-negative exit value for an incorrectly formed
        // command line,  '% java'
        tr = doExec(javaCmd);
        tr.checkNegative();
        tr.isNotZeroOutput();
        System.out.println(tr);

        // 6753938, test for non-negative exit value for an incorrectly formed
        // command line,  '% java -Xcomp'
        tr = doExec(javaCmd, "-Xcomp");
        tr.checkNegative();
        tr.isNotZeroOutput();
        System.out.println(tr);

        // 7151314, test for non-negative exit value for an incorrectly formed
        // command line, '% java -jar -W', note the bogus -W
        tr = doExec(javaCmd, "-jar", "-W");
        tr.checkNegative();
        tr.contains("Unrecognized option: -W");
        System.out.println(tr);
    }

    /*
     * Tests various dispositions of the main method, these tests are limited
     * to English locales as they check for error messages that are localized.
     */
    static void runMainMethodTests() throws FileNotFoundException {
        if (!isEnglishLocale()) {
            return;
        }

        TestResult tr = null;

        // a missing class
        createJar("MIA", new File("some.jar"), new File("Foo"),
                (String[])null);
        tr = doExec(javaCmd, "-jar", "some.jar");
        tr.contains("Error: Could not find or load main class MIA");
        System.out.println(tr);
        // use classpath to check
        tr = doExec(javaCmd, "-cp", "some.jar", "MIA");
        tr.contains("Error: Could not find or load main class MIA");
        System.out.println(tr);

        // incorrect method access
        createJar(new File("some.jar"), new File("Foo"),
                "private static void main(String[] args){}");
        tr = doExec(javaCmd, "-jar", "some.jar");
        tr.contains("Error: Main method not found in class Foo");
        System.out.println(tr);
        // use classpath to check
        tr = doExec(javaCmd, "-cp", "some.jar", "Foo");
        tr.contains("Error: Main method not found in class Foo");
        System.out.println(tr);

        // incorrect return type
        createJar(new File("some.jar"), new File("Foo"),
                "public static int main(String[] args){return 1;}");
        tr = doExec(javaCmd, "-jar", "some.jar");
        tr.contains("Error: Main method must return a value of type void in class Foo");
        System.out.println(tr);
        // use classpath to check
        tr = doExec(javaCmd, "-cp", "some.jar", "Foo");
        tr.contains("Error: Main method must return a value of type void in class Foo");
        System.out.println(tr);

        // incorrect parameter type
        createJar(new File("some.jar"), new File("Foo"),
                "public static void main(Object[] args){}");
        tr = doExec(javaCmd, "-jar", "some.jar");
        tr.contains("Error: Main method not found in class Foo");
        System.out.println(tr);
        // use classpath to check
        tr = doExec(javaCmd, "-cp", "some.jar", "Foo");
        tr.contains("Error: Main method not found in class Foo");
        System.out.println(tr);

        // incorrect method type - non-static
         createJar(new File("some.jar"), new File("Foo"),
                "public void main(String[] args){}");
        tr = doExec(javaCmd, "-jar", "some.jar");
        tr.contains("Error: Main method is not static in class Foo");
        System.out.println(tr);
        // use classpath to check
        tr = doExec(javaCmd, "-cp", "some.jar", "Foo");
        tr.contains("Error: Main method is not static in class Foo");
        System.out.println(tr);

        // amongst a potpourri of kindred main methods, is the right one chosen ?
        createJar(new File("some.jar"), new File("Foo"),
            "void main(Object[] args){}",
            "int  main(Float[] args){return 1;}",
            "private void main() {}",
            "private static void main(int x) {}",
            "public int main(int argc, String[] argv) {return 1;}",
            "public static void main(String[] args) {System.out.println(\"THE_CHOSEN_ONE\");}");
        tr = doExec(javaCmd, "-jar", "some.jar");
        tr.contains("THE_CHOSEN_ONE");
        System.out.println(tr);
        // use classpath to check
        tr = doExec(javaCmd, "-cp", "some.jar", "Foo");
        tr.contains("THE_CHOSEN_ONE");
        System.out.println(tr);

        // test for extraneous whitespace in the Main-Class attribute
        createJar(" Foo ", new File("some.jar"), new File("Foo"),
                "public static void main(String... args){}");
        tr = doExec(javaCmd, "-jar", "some.jar");
        tr.checkPositive();
        System.out.println(tr);
    }
    /*
     * tests 6968053, ie. we turn on the -Xdiag (for now) flag and check if
     * the suppressed stack traces are exposed, ignore these tests for localized
     * locales, limiting to English only.
     */
    static void runDiagOptionTests() throws FileNotFoundException {
        if (!isEnglishLocale()) { // only english version
            return;
        }
        TestResult tr = null;
        // a missing class
        createJar("MIA", new File("some.jar"), new File("Foo"),
                (String[])null);
        tr = doExec(javaCmd, "-Xdiag", "-jar", "some.jar");
        tr.contains("Error: Could not find or load main class MIA");
        tr.contains("java.lang.ClassNotFoundException: MIA");
        System.out.println(tr);

        // use classpath to check
        tr = doExec(javaCmd,  "-Xdiag", "-cp", "some.jar", "MIA");
        tr.contains("Error: Could not find or load main class MIA");
        tr.contains("java.lang.ClassNotFoundException: MIA");
        System.out.println(tr);

        // a missing class on the classpath
        tr = doExec(javaCmd, "-Xdiag", "NonExistentClass");
        tr.contains("Error: Could not find or load main class NonExistentClass");
        tr.contains("java.lang.ClassNotFoundException: NonExistentClass");
        System.out.println(tr);
    }

    static void test6894719() {
        // test both arguments to ensure they exist
        TestResult tr = null;
        tr = doExec(javaCmd,
                "-no-jre-restrict-search", "-version");
        tr.checkPositive();
        System.out.println(tr);

        tr = doExec(javaCmd,
                "-jre-restrict-search", "-version");
        tr.checkPositive();
        System.out.println(tr);
    }

    /**
     * @param args the command line arguments
     * @throws java.io.FileNotFoundException
     */
    public static void main(String[] args) throws FileNotFoundException {
        if (debug) {
            System.out.println("Starting Arrrghs tests");
        }
        quoteParsingTests();
        runBasicErrorMessageTests();
        runMainMethodTests();
        test6894719();
        runDiagOptionTests();
        if (testExitValue > 0) {
            System.out.println("Total of " + testExitValue + " failed");
            System.exit(1);
        } else {
            System.out.println("All tests pass");
        }
    }
}
